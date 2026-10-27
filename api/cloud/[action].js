const upstream='https://luma-home-atelier-d25.abloomgnome257.chatgpt.site';
const site='https://luma-home-sandy.vercel.app';
const json=(error,status)=>Response.json({error},{status,headers:{'Cache-Control':'no-store'}});
export default {
 async fetch(request){
  const action=new URL(request.url).pathname.split('/').pop();
  if(!['state','timer','sync'].includes(action))return json('Not found',404);
  if(!['GET','POST'].includes(request.method))return json('Method not allowed',405);
  const origin=request.headers.get('origin');
  if(origin&&origin!==site)return json('Origin denied',403);
  const key=request.headers.get('x-api-key')||'';
  if(key.length<24||key.length>128)return json('Invalid cloud key',401);
  let body;
  if(request.method==='POST'){
   const reader=request.body?.getReader();let size=0;const chunks=[];
   if(reader)while(true){const {done,value}=await reader.read();if(done)break;size+=value.length;if(size>2048){await reader.cancel();return json('Body too large',413)}chunks.push(value)}
   body=Buffer.concat(chunks);
  }
  try{
   const response=await fetch(upstream+'/api/cloud/'+action,{
    method:request.method,redirect:'error',signal:AbortSignal.timeout(3500),
    headers:{'X-API-Key':key,'Content-Type':request.headers.get('content-type')||'application/x-www-form-urlencoded','Origin':upstream,'Accept':'application/json'},body
   });
   return new Response(response.body,{status:response.status,headers:{'Content-Type':response.headers.get('content-type')||'application/json','Cache-Control':'no-store'}});
  }catch{return json('Cloud temporarily unavailable',502)}
 }
};
