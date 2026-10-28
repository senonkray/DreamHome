const headers={'Content-Type':'application/json; charset=utf-8','Cache-Control':'no-store'};
const json=(data,status=200)=>new Response(JSON.stringify(data)+'\n',{status,headers});
const DEFAULT={color:'#ff9e57',brightness:75,speed:35,power:false,effect:'static',remaining:0};
const EFFECTS=['static','breathe','rainbow','candle','custom','sound'],MOTIONS=['fade','jump','pulse','flicker'],REACTIONS=['level','gradient','beat','spark'];
const online=row=>row&&Date.now()-row.seen<12000;
async function auth(request,env){const key=request.headers.get('X-API-Key')||'';if(key.length<24||key.length>128||!env.LUMA_CLOUD_KEY_HASH)return false;const digest=await crypto.subtle.digest('SHA-256',new TextEncoder().encode(key));const hex=Array.from(new Uint8Array(digest),b=>b.toString(16).padStart(2,'0')).join('');return crypto.subtle.timingSafeEqual(new TextEncoder().encode(hex),new TextEncoder().encode(env.LUMA_CLOUD_KEY_HASH));}
async function body(request){const reader=request.body?.getReader();if(!reader)return '';let result='',length=0;const decoder=new TextDecoder();while(true){const {done,value}=await reader.read();if(done)break;length+=value.length;if(length>2048){await reader.cancel();throw new Error('Body too large')}result+=decoder.decode(value,{stream:true})}return result+decoder.decode()}
function stateFields(input,partial=false){const s={};const field=(name,valid,transform=x=>x)=>{if(input[name]===undefined){if(!partial)throw new Error('Missing '+name);return}if(!valid(input[name]))throw new Error('Invalid '+name);s[name]=transform(input[name])};field('color',x=>/^#?[a-f\d]{6}$/i.test(x),x=>'#'+x.replace('#','').toLowerCase());for(const [n,min,max]of [['brightness',0,100],['speed',1,100]])field(n,x=>/^\d+$/.test(String(x))&&+x>=min&&+x<=max,Number);field('power',x=>[true,false,'0','1',0,1].includes(x),x=>x===true||x===1||x==='1');field('effect',x=>EFFECTS.includes(x));
 // Custom-mode fields are always optional: older firmware neither sends nor needs them.
 const optional=(name,valid,transform)=>{if(input[name]===undefined)return;if(!valid(input[name]))throw new Error('Invalid '+name);s[name]=transform(input[name])};
 optional('palette',x=>/^#?[a-f\d]{6}(,#?[a-f\d]{6}){0,5}$/i.test(x),x=>x.toLowerCase().replace(/#/g,''));optional('motion',x=>MOTIONS.includes(x),x=>x);optional('react',x=>REACTIONS.includes(x),x=>x);optional('floor',x=>/^\d+$/.test(String(x))&&+x<=100,Number);return s;}
async function getRow(env){return env.DB.prepare("SELECT * FROM luma_device WHERE id = 'main'").first()}
function response(row){const reported=row?JSON.parse(row.reported):{};return {device:'luma-esp8266',...DEFAULT,...reported,cloud:true,cloudEngine:2,online:!!online(row),pending:!!row&&row.revision>row.applied&&Date.now()-row.issued<30000,revision:row?.revision||0,applied:row?.applied||0,lastSeen:row?.seen||0};}
export async function api(request,env){const url=new URL(request.url),path=url.pathname;
 if(!await auth(request,env))return json({error:'Invalid cloud key'},401);
 if(!env.DB)return json({error:'Cloud storage not configured'},503);
 const origin=request.headers.get('Origin');if(origin&&origin!==url.origin)return json({error:'Origin denied'},403);
 try{
 if(path==='/api/cloud/state'&&request.method==='GET')return json(response(await getRow(env)));
 if(path==='/api/cloud/sync'&&request.method==='POST'){
  const input=JSON.parse(await body(request));
  // Engine-2 firmware keeps "effect" compatible with older servers and sends the real effect as "fx".
  if(EFFECTS.includes(input.fx))input.effect=input.fx;
  const reported=stateFields(input);if(input.engine===2)reported.engine=2;if(typeof input.fw==='string'&&/^[\w.-]{1,16}$/.test(input.fw))reported.fw=input.fw;
  if(!/^[a-f\d]{8,32}$/i.test(input.boot)||!Number.isSafeInteger(input.ack)||input.ack<0)throw new Error('Invalid device report');
  reported.remaining=Number.isInteger(input.remaining)&&input.remaining>=0&&input.remaining<=86400?input.remaining:0;
  reported.micAvailable=input.micAvailable===true;reported.micLevel=Number.isInteger(input.micLevel)&&input.micLevel>=0&&input.micLevel<=100?input.micLevel:0;
  reported.rssi=Number.isInteger(input.rssi)?input.rssi:0;
  let row=await getRow(env);const now=Date.now();
  if(!row){await env.DB.prepare("INSERT OR IGNORE INTO luma_device (id) VALUES ('main')").run();row=await getRow(env)}
  const reboot=row.boot!==input.boot;const ack=reboot?row.revision:Math.min(row.revision,input.ack);
  if(reboot||ack!==row.applied||now-row.seen>=4000||JSON.stringify(reported)!==row.reported){await env.DB.prepare("UPDATE luma_device SET reported=?, seen=?, applied=?, boot=? WHERE id='main'").bind(JSON.stringify(reported),now,ack,input.boot).run()}
  const fresh=!reboot&&row.revision>ack&&now-row.issued<30000;
  return json({revision:row.revision,apply:fresh,command:fresh?JSON.parse(row.desired):null,pollMs:400});
 }
 if((path==='/api/cloud/state'||path==='/api/cloud/timer')&&request.method==='POST'){
  const input=Object.fromEntries(new URLSearchParams(await body(request)));
  const row=await getRow(env);if(!online(row))return json({error:'ESP8266 is offline'},503);
  let patch;
  if(path.endsWith('/timer')){if(!/^\d+$/.test(input.minutes)||+input.minutes>1440)throw new Error('Invalid minutes');patch={timerEnd:+input.minutes?Date.now()+(+input.minutes)*60000:0}}
  else {if(input.audio!==undefined)return json({error:'Audio sync is local-only'},400);patch=stateFields(input,true);const device=JSON.parse(row.reported);if(patch.effect==='sound'&&!device.micAvailable)return json({error:'Microphone not enabled on device'},400);if(patch.effect==='custom'&&device.engine!==2)return json({error:'Firmware update required for custom modes'},400);if(!Object.keys(patch).length)throw new Error('Empty command')}
  // json_patch merges atomically so a concurrent timer and colour update cannot overwrite each other.
  await env.DB.prepare("UPDATE luma_device SET desired=json_patch(desired, ?), revision=revision+1, issued=? WHERE id='main'").bind(JSON.stringify(patch),Date.now()).run();
  return json(response(await getRow(env)));
 }
 return json({error:'Not found'},404);
 }catch(error){if(error instanceof SyntaxError||/^(Missing|Invalid|Empty|Body)/.test(error.message))return json({error:error.message},400);console.error('cloud_api_failure',error.message);return json({error:'Cloud temporarily unavailable'},503)}
}
