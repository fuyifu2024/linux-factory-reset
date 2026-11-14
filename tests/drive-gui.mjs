// Real Zenity/X11 + pkttyagent + pkexec in an expendable VM. No dialog/backend doubles.
import fs from 'node:fs';
import cp from 'node:child_process';
const [guiPid, dir] = process.argv.slice(2);
const pause = ms => new Promise(r => setTimeout(r, ms));
const run = (cmd,args) => cp.execFileSync(cmd,args,{encoding:'utf8',timeout:15000}).trim();
const log = message => {fs.appendFileSync(dir+'/steps.txt',message+'\n');console.log('GUI QA: '+message);};
let auths=0, shotAt=0;
function dialog() {
  let pids=[]; try { pids=run('pgrep',['-u',String(process.getuid()),'-x','zenity']).split('\n'); } catch {}
  for(const pid of pids) try {
    const args=fs.readFileSync('/proc/'+pid+'/cmdline','utf8').split('\0').filter(Boolean).slice(1);
    const w=run('xdotool',['search','--onlyvisible','--pid',pid]).split('\n')[0];
    if(args.includes('--progress')) {
      if(args.includes('--pulsate') && Date.now()-shotAt>5000) {shotAt=Date.now();run('xdotool',['windowraise',w]);run('scrot',['--overwrite',dir+'/preparation-progress.png']);}
      if(args.includes('--auto-close') && Date.now()-shotAt>5000) {shotAt=Date.now();run('xdotool',['windowraise',w]);run('scrot',['--overwrite',dir+'/download.png']);}
      continue;
    }
    return {args,w,pid};
  } catch {}
}
async function tick() {
  const auth=fs.existsSync(dir+'/auth.txt')?fs.readFileSync(dir+'/auth.txt','utf8'):'';
  const prompts=(auth.match(/Password:/g)||[]).length;
  if(prompts>auths) {
    auths=prompts;const w=run('xdotool',['search','--onlyvisible','--name','^LFR authorization$']).split('\n')[0];
    run('xdotool',['windowactivate','--sync',w]);run('xdotool',['type','--clearmodifiers','qa7reset92factory']);run('xdotool',['key','Return']);
    log('Authenticated real polkit prompt '+auths);
  }
  await pause(250);
}
async function waitFor(predicate, label, timeout=120000) {
  log('Waiting for '+label);
  const end=Date.now()+timeout;while(Date.now()<end){await tick();const d=dialog();if(d&&predicate(d))return d;}throw Error('GUI timeout: '+label);
}
async function waitMarker(name) {
  const end=Date.now()+300000;log('Waiting for marker '+name);
  while(Date.now()<end){await tick();if(fs.existsSync(dir+'/'+name))return;}
  throw Error('GUI root verification timed out: '+name);
}
async function keys(d, ...values) {run('xdotool',['windowactivate','--sync',d.w]);run('xdotool',['key','--clearmodifiers',...values]);await pause(400);}
async function paste(d,value) {
  cp.execFileSync('xclip',['-selection','clipboard'],{input:value,stdio:['pipe','ignore','ignore'],timeout:15000});
  await keys(d,'ctrl+a','ctrl+v');
}
async function main(row) {const d=await waitFor(d=>d.args.includes('--column=状态'),'main');await keys(d,'Home',...Array(row).fill('Down'),'Return');}
async function info(expected, timeout=120000) {
  const d=await waitFor(d=>d.args.includes('--text-info'),'result',timeout);
  const arg=d.args.find(x=>x.startsWith('--filename='));const text=fs.readFileSync(arg.slice(11),'utf8');
  fs.appendFileSync(dir+'/messages.txt',text+'\n');
  if(expected&&!text.includes(expected))throw Error('Unexpected GUI result: '+text.slice(0,1200));
  await keys(d,'alt+F4');return text;
}
async function chooseDirectory(path) {
  const d=await waitFor(d=>d.args.includes('--directory'),'directory');
  await keys(d,'ctrl+l');await paste(d,path+'/');
  run('scrot',['--overwrite',dir+(path==='/mnt/recovery'?'/directory-recovery.png':'/directory-download.png')]);
  await keys(d,'Return');
  // Enter in GTK's location entry navigates into a folder. Activate the real
  // Select button as a separate action (the application uses the C locale).
  await keys(d,'alt+s');log('Selected directory '+path);
}
async function confirm(value) {
  const d=await waitFor(d=>d.args.includes('--entry'),'confirmation');
  await paste(d,value);await keys(d,'Return');
}
try {
  await main(2);await info('请先选择并准备恢复镜像');log('Unprepared preview guarded');
  await main(1);
  let d=await waitFor(d=>d.args.includes('--column=来源'),'source');await keys(d,'Home','Down','Return');
  await info('请选择独立 ext4');await chooseDirectory('/mnt/recovery');
  await info('请选择一个用于暂存');await chooseDirectory('/home/lfr_old_user');
  await info('操作已完成',900000);log('Official download and privileged image preparation completed');
  await main(2);await info('这是只读预览');log('Actual root/ESP preview completed');
  await main(3);await confirm('wrong confirmation');
  await waitFor(d=>d.args.includes('--column=状态'),'wrong-confirmation return');log('Wrong destructive confirmation rejected');
  fs.writeFileSync(dir+'/request-arm','1');
  await waitMarker('armed');
  await main(4);await info('Pending reset cancelled');
  fs.writeFileSync(dir+'/request-check-cancel','1');
  await waitMarker('cancel-verified');
  log('GUI cancelled an actual armed reset; root checked marker and BootOrder');
  await main(3);fs.writeFileSync(dir+'/begin-restore','1');await confirm('清空全部用户');
  // Final arm has its own real polkit prompt. Keep the agent driven until the
  // privileged backend actually writes its request, rather than just typing OK.
  await waitMarker('restore-armed');
  log('Real GUI authenticated and armed confirmed reset/reboot');
  fs.writeFileSync(dir+'/gui-driver-complete','1');
} catch(e) {
  log(e.stack);try{run('scrot',['--overwrite',dir+'/failure.png']);}catch{}
  const d=dialog();if(d)log('Remaining real dialog: '+JSON.stringify(d.args));
  process.exit(1);
}
