sap.ui.define([
  'sap/m/VBox',
  'sap/ui/core/HTML',
  'sap/dm/dme/pod2/widget/Widget',
  'sap/dm/dme/pod2/widget/metadata/WidgetProperty',
  'sap/dm/dme/pod2/propertyeditor/StringPropertyEditor',
  'sap/dm/dme/pod2/propertyeditor/BooleanPropertyEditor',
  'sap/dm/dme/pod2/propertyeditor/PropertyCategory',
  'sap/dm/dme/pod2/context/PodContext',
  'sap/dm/dme/pod2/model/I18nResourceModel',
  './State', './Dashboard', './Connection', '../lib/Mqtt'
], function(VBox, HTML, Widget, WidgetProperty, StringPropertyEditor, BooleanPropertyEditor, PropertyCategory, PodContext, I18nResourceModel, State, Dashboard, Connection, mqtt) {
  'use strict';
  const base='custom/pod2/mqttdashboard';
  const defaults={brokerUrl:'',topicPrefix:'factory',profile:'brewery',profileJson:'',title:'Shop floor live',staleSeconds:'30',showDiagnostics:true};
  const profiles={brewery:'brewery',factory:'factory',bakery:'bakery',ev_battery:'ev_battery'};
  let i18n;

  class MqttDashboard extends Widget {
    static getDisplayName() {return 'MQTT Live Dashboard';}
    static getDescription() {return 'Live machines, conical tanks and operator badges. Incoming signals update existing cards.';}
    static getIcon() {return 'sap-icon://sys-monitor';}
    static getCategory() {return 'Connected Shop Floor';}
    static getI18nModel() {return i18n ||= new I18nResourceModel({bundleName:'custom.pod2.mqttdashboard.i18n.i18n'});}
    static getDefaultConfig() {return {properties:{...defaults}};}
    getPropertyValue(name) {const value=super.getPropertyValue(name);return value===undefined || value===null?defaults[name]:value;}
    getProperties() {
      const definitions=[
        ['brokerUrl','Public broker URL','Your public wss:// MQTT endpoint. Keep deployment addresses out of source code.'],
        ['topicPrefix','Topic prefix','The device topic prefix, normally factory.'],
        ['profile','Device profile','brewery, factory, bakery or ev_battery. Match the active device profile.'],
        ['profileJson','Custom device profile','Optional device configuration JSON, overriding the built-in profile.'],
        ['title','Dashboard title','Title displayed above the readings.'],
        ['staleSeconds','Device timeout (seconds)','15–3600. Default 30; device diagnostics arrive every 10 seconds.']
      ];
      return [...(super.getProperties()||[]),...definitions.map(([key,displayName,description])=>new WidgetProperty({displayName,description,category:PropertyCategory.Main,propertyEditor:new StringPropertyEditor(this,key)})),new WidgetProperty({displayName:'Show connection details',category:PropertyCategory.Main,propertyEditor:new BooleanPropertyEditor(this,'showDiagnostics',true)})];
    }
    _createView() {
      // POD may request the view again. Reuse its controls and their exact IDs.
      if(this._root)return this._root;
      const config=this.getConfig();
      if(!config?.id)throw Error('MQTT dashboard needs a POD widget ID.');
      this._closed=false;
      const htmlId=config.id+'--mqtt-content';
      const escaped=htmlId.replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
      // The HTML root and UI5 HTML control share one ID. setContent is never
      // called on a signal update; UI5 can safely preserve this single root.
      this._html=new HTML(htmlId,{content:`<div id="${escaped}" class="dm-mqtt-dashboard"><p role="status">Loading dashboard…</p></div>`,preferDOM:true,sanitizeContent:false,afterRendering:()=>this._mount()});
      this._root=new VBox(config.id,{width:'100%',items:[this._html]});
      if(!document.getElementById('dm-mqtt-dashboard-styles')) {
        const link=document.createElement('link');link.id='dm-mqtt-dashboard-styles';link.rel='stylesheet';link.href=sap.ui.require.toUrl(base+'/assets/dashboard.css');document.head.append(link);
      }
      return this._root;
    }
    async onInit() {
      if(this._initializing)return this._initializing;
      this._initializing=this._initialize();return this._initializing;
    }
    async _initialize() {
      await super.onInit();
      if(this._closed)return;
      this._abort=new AbortController();
      try {
        const custom=this.getPropertyValue('profileJson')||'';let profile;
        if(custom.trim()) {
          if(custom.length>262144)throw Error('The custom profile is too large.');
          profile=JSON.parse(custom);
        } else {
          const key=this.getPropertyValue('profile');
          if(!Object.hasOwn(profiles,key))throw Error('Choose brewery, factory, bakery or ev_battery.');
          const response=await fetch(sap.ui.require.toUrl(base+'/config/'+profiles[key]+'.json'),{signal:this._abort.signal});
          if(!response.ok)throw Error('The bundled device profile could not load.');
          profile=await response.json();
        }
        if(this._closed)return;
        this._state=new State(profile,{prefix:this.getPropertyValue('topicPrefix'),staleSeconds:Number(this.getPropertyValue('staleSeconds'))});
        this._connection=new Connection(mqtt,this._state,{brokerUrl:this.getPropertyValue('brokerUrl'),secure:location.protocol==='https:',changed:()=>this._renderer?.schedule()});
        if(PodContext.isRunMode())this._connection.start();else this._state.seedPreview(profile);
        this._mount();
      } catch(error) {
        if(this._closed || error.name==='AbortError')return;
        this._error=error instanceof SyntaxError?'The custom profile is not valid JSON.':error.message;
        this._mount();
      }
    }
    _mount() {
      if(this._closed)return;
      const dom=this._html?.getDomRef();if(!dom)return;
      if(this._error){dom.textContent=this._error;return;}
      if(!this._state || (this._renderer && this._renderer.root===dom))return;
      this._renderer?.destroy();
      this._renderer=new Dashboard(dom,this._state,{title:this.getPropertyValue('title'),logoUrl:sap.ui.require.toUrl(base+'/assets/sap-logo.svg'),showDiagnostics:this.getPropertyValue('showDiagnostics')!==false,refresh:()=>this._connection?.requestSnapshot(),reconnect:()=>this._connection?.reconnect()});
    }
    onExit() {
      this._closed=true;this._abort?.abort();this._connection?.destroy();this._renderer?.destroy();
      this._connection=null;this._renderer=null;this._state=null;
      super.onExit();
    }
  }
  return MqttDashboard;
});
