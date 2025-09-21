const sip = require('./build/Release/baresip_node.node');

sip.init((e) => console.log('evt', e));
await sip.register({
  aor: 'sip:alice@example.com;transport=tls',
  authUser: 'alice',
  password: 'secret'
});

// wait for register_ok event, then:
const id = sip.invite('sip:bob@example.com;transport=udp');

// poll stats
setInterval(() => console.log('stats', sip.getStats(id)), 1000);

// later:
sip.hangup(id);
sip.shutdown();
