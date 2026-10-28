/* Only the newest unsent light state matters. Never build a slider backlog. */
class LumaTransport {
  constructor(send,{gap=120,retryDelay=300,onSuccess=()=>{},onError=()=>{}}={}){Object.assign(this,{send,gap,retryDelay,onSuccess,onError});this.pending=null;this.busy=false;this.timer=null;this.last=0;this.epoch=0;}
  enqueue(data,{ephemeral=false,immediate=false}={}){if(ephemeral&&(this.busy||this.pending))return;this.pending={data,ephemeral,retries:0};this.schedule(immediate?0:undefined);}
  reset(){this.epoch++;this.pending=null;clearTimeout(this.timer);this.timer=null;}
  schedule(delay=0){if(this.busy||this.timer||!this.pending)return;this.timer=setTimeout(()=>{this.timer=null;this.flush()},Math.max(delay,this.gap-(Date.now()-this.last),0));}
  async flush(){if(this.busy||!this.pending)return;const item=this.pending,epoch=this.epoch;this.pending=null;this.busy=true;this.last=Date.now();let retry=0;
    try{const result=await this.send(item.data);if(epoch===this.epoch)this.onSuccess(result,Date.now()-this.last)}
    catch(error){if(epoch===this.epoch){if(!this.pending&&!item.ephemeral&&item.retries<2&&(!error.status||error.status>=500)){this.pending={...item,retries:item.retries+1};retry=this.retryDelay*(item.retries+1)}else if(!this.pending&&!item.ephemeral)this.onError(error)}}
    finally{this.busy=false;this.schedule(retry)}
  }
}
if(typeof module!=='undefined')module.exports=LumaTransport;
if(typeof window!=='undefined')window.LumaTransport=LumaTransport;
