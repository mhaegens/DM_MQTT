(function(factory) {
  if(typeof sap!=='undefined' && sap.ui?.define) sap.ui.define([],factory);
  else if(typeof module==='object' && module.exports) module.exports=factory();
  else globalThis.DmMqttConnection=factory();
})(function() {
  'use strict';
  class Connection {
    constructor(mqtt,state,{brokerUrl='',changed=()=>{},secure=true}={}) {
      this.mqtt=mqtt;this.state=state;this.url=brokerUrl;this.changed=changed;this.secure=secure;this.client=null;this.closed=false;
    }
    start() {
      if(this.closed || this.client)return;
      if(!this.url) {this.state.setConnection('idle','Set the broker URL in POD Designer to connect.');this.changed();return;}
      let url;
      try {
        url=new URL(this.url);
        if(!['ws:','wss:'].includes(url.protocol) || (this.secure && url.protocol!=='wss:'))throw Error();
        if(url.username || url.password || url.hash)throw Error();
      } catch(_) {this.state.setConnection('error','Enter a valid public wss:// broker URL without embedded credentials.');this.changed();return;}
      this.state.setConnection('connecting');this.changed();
      let client;
      try {
        client=this.mqtt.connect(url.href,{clientId:'dm-pod-'+Math.random().toString(36).slice(2,12),clean:true,reconnectPeriod:1000,connectTimeout:10000,keepalive:30,queueQoSZero:false});
      } catch(_) {this.state.setConnection('error','The broker connection could not start.');this.changed();return;}
      this.client=client;
      const current=()=>!this.closed && this.client===client;
      const disconnected=()=>{if(current()){this.state.setConnection('connecting','Reconnecting to the broker…');this.changed();}};
      client.on('connect',()=>{
        if(!current())return;
        this.state.resetReadings();this.state.setConnection('connected');this.changed();
        client.subscribe(this.state.prefix+'/#',{qos:0},(error,granted)=>{
          if(!current())return;
          if(error || !Array.isArray(granted) || !granted.length || granted.some(g=>g.qos===128)) {
            this.state.setConnection('error','The broker rejected the telemetry subscription.');this.changed();return;
          }
          this.requestSnapshot();
        });
      });
      client.on('message',(topic,payload)=>{
        if(!current())return;
        if(payload.length>8192){this.state.invalid++;return;}
        if(this.state.apply(topic,payload.toString()))this.changed();
      });
      client.on('offline',disconnected);client.on('close',disconnected);client.on('reconnect',disconnected);
      client.on('error',()=>{if(current()){this.state.setConnection('error','Broker connection failed. Check the URL, access policy and tenant connection policy.');this.changed();}});
    }
    requestSnapshot() {
      const client=this.client;
      if(this.closed || !client?.connected)return;
      client.publish(this.state.prefix+'/device/request_snapshot','state',{qos:0,retain:false},error=>{
        if(error && !this.closed && client===this.client){this.state.message='The state refresh could not be sent.';this.changed();}
      });
    }
    reconnect() {
      if(this.closed)return;
      this.stopClient();this.start();
    }
    stopClient() {
      const client=this.client;this.client=null;
      if(client){client.removeAllListeners();client.end(true);}
    }
    destroy() {this.closed=true;this.stopClient();this.changed=()=>{};}
  }
  return Connection;
});
