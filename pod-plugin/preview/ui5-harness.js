/* Test doubles cover only tenant-specific SDK modules. VBox, HTML and their
   preservation/re-rendering behavior come from real OpenUI5. Not in the ZIP. */
(function(){
  'use strict';
  sap.ui.define('sap/dm/dme/pod2/widget/Widget',[],function(){
    return class {
      constructor(config){this.config=config;}
      getConfig(){return this.config;}
      getPropertyValue(key){return this.config.properties[key];}
      getProperties(){return [];}
      async onInit(){}
      onExit(){}
    };
  });
  for(const name of ['widget/metadata/WidgetProperty','propertyeditor/StringPropertyEditor','propertyeditor/BooleanPropertyEditor'])sap.ui.define('sap/dm/dme/pod2/'+name,[],()=>class{constructor(...args){this.args=args;}});
  sap.ui.define('sap/dm/dme/pod2/propertyeditor/PropertyCategory',[],()=>({Main:'Main'}));
  sap.ui.define('sap/dm/dme/pod2/context/PodContext',[],()=>({isRunMode:()=>true}));
  sap.ui.define('sap/dm/dme/pod2/model/I18nResourceModel',['sap/ui/model/resource/ResourceModel'],ResourceModel=>ResourceModel);
  sap.ui.getCore().attachInit(function(){
    sap.ui.require(['custom/pod2/mqttdashboard/widget/MqttDashboard','sap/ui/core/Core'],async function(Type,Core){
      const params=new URLSearchParams(location.search);
      const widget=new Type({id:'mqtt-ui5-widget',properties:{brokerUrl:params.get('broker')||'',profile:'brewery'}});
      const root=widget._createView();root.placeAt('pod-container');Core.applyChanges();await widget.onInit();Core.applyChanges();
      window.ui5Harness={widget,root,Core};
    });
  });
})();
