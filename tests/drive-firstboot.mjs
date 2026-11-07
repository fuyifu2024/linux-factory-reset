// Disposable remote VM driver. No browser, host account or real user's credentials.
import fs from 'node:fs';
import net from 'node:net';
const [socketPath, logPath] = process.argv.slice(2);
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const deadline = Date.now() + 16 * 60 * 1000;
let typed = false;
while (Date.now() < deadline) {
  const log = fs.existsSync(logPath) ? fs.readFileSync(logPath, 'utf8') : '';
  if (/LFR_VM_(?:PREPARATION_FAIL|OLD_DATA_SURVIVED|NEW_USER_FAIL|SETUP_TIMEOUT)/.test(log)) throw Error('Real VM reported a failed recovery; inspect serial evidence');
  if (log.includes('LFR_VM_RESTORE_PASS')) { await delay(5000); console.log('Real UEFI VM restored, removed old users/data and created a new sudo administrator.'); process.exit(0); }
  if (!typed && log.includes('LFR_VM_AWAITING_NEW_USER')) {
    typed = true;
    const socket = net.createConnection(socketPath);
    await new Promise((resolve, reject) => { socket.once('connect', resolve); socket.once('error', reject); });
    socket.on('data', () => {});
    socket.write(JSON.stringify({execute:'qmp_capabilities'}) + '\n');
    await delay(1000);
    const key = async value => { socket.write(JSON.stringify({execute:'human-monitor-command',arguments:{'command-line':`sendkey ${value}`}})+'\n'); await delay(90); };
    for (const value of ['lfrqatest', 'qa7reset92factory', 'qa7reset92factory']) {
      for (const char of value) await key(char);
      await key('ret'); await delay(1000);
    }
    socket.end();
  }
  await delay(1000);
}
throw Error('Real VM recovery timed out; boot/installer success is NOT established');
