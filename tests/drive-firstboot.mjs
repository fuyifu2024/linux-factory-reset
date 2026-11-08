// Disposable remote VM driver. No browser, host account or real user's credentials.
import fs from 'node:fs';
import net from 'node:net';
const [socketPath, logPath, fault = "none"] = process.argv.slice(2);
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const deadline = Date.now() + Number(process.env.VM_TIMEOUT_MINUTES || 30) * 60 * 1000;
const recoveryLimit = process.env.SOURCE_ARCH === 'arm64' ? 1800000 : 600000;
let typed = false, armedAt = 0, setupAt = 0, setups = 0, phaseOffset = -1, inputStage = 0;
async function typeLine(value) {
  const socket = net.createConnection(socketPath);
  await new Promise((resolve, reject) => { socket.once('connect', resolve); socket.once('error', reject); });
  socket.on('data', () => {});
  socket.write(JSON.stringify({execute:'qmp_capabilities'}) + '\n');
  await delay(1000);
  for (const key of [...value, 'ret']) {
    socket.write(JSON.stringify({execute:'human-monitor-command',arguments:{'command-line':`sendkey ${key} 40`}})+'\n');
    await delay(90);
  }
  socket.end();
}
while (Date.now() < deadline) {
  const log = fs.existsSync(logPath) ? fs.readFileSync(logPath, 'utf8') : '';
  // TCG can spend minutes in each real phase. Reset the phase deadline only
  // for a new production milestone; the overall deadline remains unchanged.
  if (process.env.SOURCE_ARCH === 'arm64') {
    for (const marker of ['LFR_VM_READY_TO_ARM',
      'Ubuntu Reset: verifying recovery media; do not remove power.',
      'Linux Factory Reset: saving temporary rollback copy.',
      'Ubuntu Reset: removing users/data and restoring initial system files.',
      'Ubuntu Reset complete. Restarting into initial setup.']) {
      const offset = log.lastIndexOf(marker);
      if (offset > phaseOffset) {phaseOffset = offset; armedAt = Date.now(); console.log('ARM recovery phase: '+marker);}
    }
  }
  if (fault === 'powercut' && log.includes('LFR_VM_POWER_CUT_READY')) process.exit(42);
  if (log.includes('LFR_VM_ROLLBACK_PASS')) { await delay(4000); process.exit(0); }
  const seen = (log.match(/LFR_VM_AWAITING_NEW_USER/g) || []).length;
  if (seen > setups) { setups = seen; typed = false; setupAt = 0; inputStage = 0; armedAt = Date.now(); }
  if (!armedAt && log.includes('LFR_VM_READY_TO_ARM')) armedAt = Date.now();
  if (fault === 'none' && armedAt && !log.includes('LFR_VM_AWAITING_NEW_USER') && Date.now() - armedAt > recoveryLimit) throw Error('Recovery did not reach initial account setup within '+recoveryLimit/60000+' minutes; inspect serial evidence');
  if (setupAt && !(log.includes('LFR_VM_ACCOUNT_PASS '+setups) || log.includes('LFR_VM_RESTORE_PASS')) && Date.now() - setupAt > 190000) throw Error('Initial account setup did not complete; inspect serial evidence');
  if (/ALERT!.*does not exist|Ubuntu Reset STOPPED|Kernel panic - not syncing|grub rescue>|Entering emergency mode|You are in emergency mode|No bootable device/.test(log)) throw Error('Guest boot/recovery halted; inspect serial evidence');
  if (/Linux Factory Reset STOPPED: (?!expected QA write failure)/.test(log)) throw Error('Recovery refused to proceed; inspect serial evidence');
  if (/LFR_VM_(?:PREPARATION_FAIL|OLD_DATA_SURVIVED|NEW_USER_FAIL|SETUP_TIMEOUT|SELINUX_FAIL)/.test(log)) throw Error('Real VM reported a failed recovery; inspect serial evidence');
  if (/Failed to start .*lfr-qa\.service/.test(log)) throw Error('Disposable guest preparation service failed; inspect serial evidence');
  if (typed && log.includes('reboot: Power down')) { console.log('Guest powered off; the host must verify its synced result and restored filesystem.'); process.exit(0); }
  if (log.includes('LFR_VM_ALL_CYCLES_COMPLETE') || log.includes('LFR_VM_RESTORE_PASS')) { await delay(5000); console.log('Real UEFI VM restored, removed old users/data and created a new sudo administrator.'); process.exit(0); }
  if (process.env.VM_PID) {
    const proc='/proc/'+process.env.VM_PID+'/stat';
    if (!fs.existsSync(proc) || /^\d+ \(.*\) Z /.test(fs.readFileSync(proc,'utf8'))) throw Error('QEMU exited before a verified result; inspect qemu.txt and serial evidence');
  }
  if (seen) {
    const current = log.slice(log.lastIndexOf('LFR_VM_AWAITING_NEW_USER'));
    const prompts = ['Username / 用户名:', 'New password:', 'Retype new password:'];
    const values = ['lfrqatest', 'qa7reset92factory', 'qa7reset92factory'];
    if (inputStage < prompts.length && current.includes(prompts[inputStage])) {
      if (!typed) {typed = true; setupAt = Date.now();}
      // passwd may discard bytes typed before its prompt. Never type the next
      // field on a fixed delay, particularly under slow ARM system emulation.
      await typeLine(values[inputStage]);
      console.log('Entered first-user field '+(++inputStage)+' after its actual prompt.');
    }
  }
  await delay(1000);
}
throw Error('Real VM recovery timed out; boot/installer success is NOT established');
