#include <napi.h>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <string>

extern "C" {
  #include <re.h>
  #include <baresip.h>
}

// ---------------------- Globals ----------------------

static std::thread sipThread;
static std::atomic<bool> running{false};
static Napi::ThreadSafeFunction tsfn;

static struct ua* g_ua = nullptr;  // default UA (created in register())

// Track ONLY calls we originate (outgoing) so we can control them by id.
// (Incoming call IDs will be added once we confirm how to extract the call* from bevent)
static std::unordered_map<struct call*, uint32_t> g_calls;
static std::atomic<uint32_t> g_nextId{1};

static uint32_t id_for_call(struct call* c) {
  auto it = g_calls.find(c);
  if (it != g_calls.end()) return it->second;
  uint32_t id = g_nextId++;
  g_calls[c] = id;
  return id;
}

static struct call* call_for_id(uint32_t id) {
  for (auto &kv : g_calls) if (kv.second == id) return kv.first;
  return nullptr;
}

// ---------------------- re() thread ----------------------

static void re_thread() {
  re_main(NULL);  // blocks until re_cancel()
}

// ---------------------- Event bridge (bevent) ----------------------

static const char* ev_to_str(enum bevent_ev ev) {
  switch (ev) {
    case BEVENT_REGISTERING:       return "registering";
    case BEVENT_REGISTER_OK:       return "register_ok";
    case BEVENT_REGISTER_FAIL:     return "register_fail";
    case BEVENT_UNREGISTERING:     return "unregistering";
    case BEVENT_MWI_NOTIFY:        return "mwi_notify";
    case BEVENT_CALL_INCOMING:     return "call_incoming";
    case BEVENT_CALL_LOCAL_SDP:    return "call_local_sdp";
    case BEVENT_CALL_REMOTE_SDP:   return "call_remote_sdp";
    case BEVENT_CALL_PROGRESS:     return "call_progress";
    case BEVENT_CALL_RINGING:      return "call_ringing";
    case BEVENT_CALL_ESTABLISHED:  return "call_established";
    case BEVENT_CALL_CLOSED:       return "call_closed";
    default:                       return "unknown";
  }
}

// Signature: void (*bevent_h)(enum bevent_ev, struct bevent*, void*)
static void bevent_handler(enum bevent_ev ev, struct bevent *be, void *arg) {
  (void)be; (void)arg;
  if (!tsfn) return;

  const char* evname = ev_to_str(ev);

  // NOTE: We’re not dereferencing `be` (your headers lack bevent_ua/call/prm helpers).
  // We’ll just emit the type for now.
  tsfn.BlockingCall([evname](Napi::Env env, Napi::Function jsCb) {
    Napi::Object e = Napi::Object::New(env);
    e.Set("type", evname);
    jsCb.Call({ e });
  });
}

// ---------------------- N-API: init / shutdown ----------------------

Napi::Value InitBaresip(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  // optional callback: init(cb)
  if (info.Length() > 0) {
    if (!info[0].IsFunction()) {
      Napi::TypeError::New(env, "init([callback])").ThrowAsJavaScriptException();
      return env.Null();
    }
    tsfn = Napi::ThreadSafeFunction::New(env, info[0].As<Napi::Function>(),
                                         "baresip-events", 0, 1);
  }

  if (running.load()) return Napi::Boolean::New(env, true);

  int err = 0;

  // libre + baresip core
  err = libre_init();
  if (err) { Napi::Error::New(env, "libre_init failed").ThrowAsJavaScriptException(); return env.Null(); }

  err = baresip_init(conf_config());
  if (err) { Napi::Error::New(env, "baresip_init failed").ThrowAsJavaScriptException(); return env.Null(); }

  // module system (static modules you compiled in)
  mod_init();

  // SIP transports: UDP/TCP/TLS
  err = ua_init("baresip node", true, true, true);
  if (err) { Napi::Error::New(env, "ua_init failed").ThrowAsJavaScriptException(); return env.Null(); }

  // register for bus events
  bevent_register(bevent_handler, nullptr);

  running = true;
  sipThread = std::thread(re_thread);
  return Napi::Boolean::New(env, true);
}

Napi::Value Shutdown(const Napi::CallbackInfo& info) {
  (void)info;
  if (!running.load()) return Napi::Boolean::New(info.Env(), true);

  bevent_unregister(bevent_handler); // unregister handler

  re_cancel();
  if (sipThread.joinable()) sipThread.join();

  ua_close();
  mod_close();
  baresip_close();
  libre_close();

  if (tsfn) { tsfn.Release(); tsfn = {}; }
  g_calls.clear();
  g_ua = nullptr;
  running = false;
  return Napi::Boolean::New(info.Env(), true);
}

// ---------------------- N-API: register / call control ----------------------

Napi::Value RegisterUA(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!running.load()) { Napi::Error::New(env, "init() first").ThrowAsJavaScriptException(); return env.Null(); }

  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "register({aor, authUser, password, srtp})").ThrowAsJavaScriptException();
    return env.Null();
  }
  auto o = info[0].As<Napi::Object>();

  auto aor      = o.Get("aor").ToString().Utf8Value();           // e.g. "sip:alice@example.com;transport=tls"
  auto authUser = o.Has("authUser") ? o.Get("authUser").ToString().Utf8Value() : "";
  auto password = o.Has("password") ? o.Get("password").ToString().Utf8Value() : "";
  // auto srtp     = o.Has("srtp") ? o.Get("srtp").ToString().Utf8Value() : "sdes"; // "sdes" or "none"

  // Baresip account line (no file needed)
  std::string acc = "<" + aor + ">";
  if (!authUser.empty()) acc += ";auth_user=" + authUser;
  if (!password.empty()) acc += ";auth_pass=" + password;
  // if (srtp == "sdes")    acc += ";mediaenc=srtp";        // requires srtp module built in
  // else if (srtp == "dtls") acc += ";mediaenc=dtls_srtp"; // requires dtls_srtp module

  int err = ua_alloc(&g_ua, acc.c_str());
  if (err || !g_ua) { Napi::Error::New(env, "ua_alloc failed").ThrowAsJavaScriptException(); return env.Null(); }

  ua_register(g_ua); // async; watch register_ok/fail events
  return Napi::Boolean::New(env, true);
}

Napi::Value Invite(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!running.load()) { Napi::Error::New(env, "init() first").ThrowAsJavaScriptException(); return env.Null(); }
  if (!g_ua) { Napi::Error::New(env, "register() first").ThrowAsJavaScriptException(); return env.Null(); }
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "invite('sip:bob@example.com')").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string dst = info[0].As<Napi::String>().Utf8Value();
  struct call *call = nullptr;

  // match your header: int ua_connect(ua*, call**, const char* from_uri, const char* req_uri, enum vidmode vmode);
  int err = ua_connect(g_ua, &call, NULL, dst.c_str(), VIDMODE_OFF);
  if (err || !call) { Napi::Error::New(env, "ua_connect failed").ThrowAsJavaScriptException(); return env.Null(); }

  uint32_t id = id_for_call(call);
  return Napi::Number::New(env, id);
}

Napi::Value Answer(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "answer(callId)").ThrowAsJavaScriptException();
    return env.Null();
  }
  uint32_t id = info[0].As<Napi::Number>().Uint32Value();
  struct call* c = call_for_id(id);
  if (!c) { Napi::Error::New(env, "call not found").ThrowAsJavaScriptException(); return env.Null(); }

  // match your header: int call_answer(call*, uint16_t scode, enum vidmode vmode);
  int err = call_answer(c, 200, VIDMODE_OFF);
  if (err) { Napi::Error::New(env, "call_answer failed").ThrowAsJavaScriptException(); return env.Null(); }

  return Napi::Boolean::New(env, true);
}

Napi::Value Hangup(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "hangup(callId)").ThrowAsJavaScriptException();
    return env.Null();
  }
  uint32_t id = info[0].As<Napi::Number>().Uint32Value();
  struct call* c = call_for_id(id);
  if (!c) { Napi::Error::New(env, "call not found").ThrowAsJavaScriptException(); return env.Null(); }

  call_hangup(c, 0, NULL);
  g_calls.erase(c);
  return Napi::Boolean::New(env, true);
}

// ---------------------- N-API: RTP stats (poll) ----------------------
// NOTE: Stubbed for now — your headers lack the stream/rtp accessors we referenced earlier.
// We’ll wire this to the exact symbols from your installed headers next pass.
Napi::Value GetStats(const Napi::CallbackInfo& info) {
  (void)info;
  return Napi::Object::New(info.Env());
}

// ---------------------- N-API: module exports ----------------------

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("init",      Napi::Function::New(env, InitBaresip));
  exports.Set("shutdown",  Napi::Function::New(env, Shutdown));
  exports.Set("register",  Napi::Function::New(env, RegisterUA));
  exports.Set("invite",    Napi::Function::New(env, Invite));
  exports.Set("answer",    Napi::Function::New(env, Answer));
  exports.Set("hangup",    Napi::Function::New(env, Hangup));
  exports.Set("getStats",  Napi::Function::New(env, GetStats));
  return exports;
}
NODE_API_MODULE(baresip_node, Init)
