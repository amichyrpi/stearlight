const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const source=fs.readFileSync(`${__dirname}/../linux/desktop-ui-bridge.js`,'utf8').replace('__STEARLIGHT_DESKTOP_TOKEN__','"test-token"');
(async()=>{
 let tick,restarts=0,nativeID,channel='beta',fail=false,now=1000,boosts=0,historyCalls=0;
 const listeners={},document={hidden:false,addEventListener(name,fn){listeners[name]=fn;}};
 const settings={vecAvailableClientBetas:[],nSelectedBetaID:-1};
 const context=vm.createContext({setInterval(fn){tick=fn;},setTimeout(){},console,
  document,performance:{now:()=>now},URL,location:{href:'https://steamloopback.host/library'},
  addEventListener(name,fn){listeners[name]=fn;},
  history:{pushState(){historyCalls++;return 37;},replaceState(){historyCalls++;return 38;}},
  settingsStore:{settings},
  fetch:async(_url,options)=>{const request=JSON.parse(options.body);if(request.operation==='performance-boost')boosts++;if(request.operation==='client-channel')channel=request.channel;return {ok:true,json:async()=>({ok:!fail,value:request.operation==='status'?{updates:{client_channel:channel,frame_version:'0.4.5',frame_build:'20261007.6125817'}}:{client_channel:channel}})};},
  SteamClient:{OpenVR:{VROverlay:{}},Settings:{SelectClientBeta(id){nativeID=id;}},User:{StartRestart(){restarts++;}},System:{GetSystemInfo:async()=>({sOSName:'Alpine',sOSVersionId:'old',nSteamVersion:123})}}
 });
 vm.runInContext(source,context);await tick();
 listeners.click();listeners.keydown();assert.equal(boosts,1);
 now+=750;document.hidden=true;listeners.click();assert.equal(boosts,1);
 document.hidden=false;
 assert.equal(context.history.pushState({},'', '/store'),37);assert.equal(boosts,2);
 now+=750;
 assert.equal(context.history.replaceState({},'', '/library'),38);assert.equal(boosts,2);
 assert.equal(historyCalls,2);
 listeners.focusin();assert.equal(boosts,3);
 assert.equal(settings.vecAvailableClientBetas.length,2);assert.equal(settings.nSelectedBetaID,2147483601);
 const info=await context.SteamClient.System.GetSystemInfo();assert.equal(info.sOSVersionId,'0.4.5');assert.equal(info.nSteamVersion,123);
 await context.SteamClient.Settings.SelectClientBeta(2147483600);assert.equal(channel,'rel');assert.equal(restarts,1);
 await context.SteamClient.Settings.SelectClientBeta(8);assert.equal(nativeID,8);assert.equal(restarts,1);
 fail=true;await assert.rejects(context.SteamClient.Settings.SelectClientBeta(2147483601));assert.equal(restarts,1);
 console.log('Native channel fallback, public-feed selection, real runtime version and failure propagation passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
