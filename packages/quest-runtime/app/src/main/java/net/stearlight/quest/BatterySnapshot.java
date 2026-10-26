package net.stearlight.quest;

/** One coherent Android battery sample; temporary unavailable readings are not percentages. */
final class BatterySnapshot {
 final int percent,status,plugged,voltage,temperature;
 final long sampled;
 BatterySnapshot(){this(-1,1,0,0,0,0);}
 private BatterySnapshot(int percent,int status,int plugged,int voltage,int temperature,long sampled){
  this.percent=percent;this.status=status;this.plugged=plugged;this.voltage=voltage;this.temperature=temperature;this.sampled=sampled;
 }
 BatterySnapshot update(int level,int scale,int status,int plugged,int voltage,int temperature,long now){
  boolean valid=scale>0&&level>=0&&level<=scale;
  int value=valid?(int)Math.round(level*100.0/scale):now-sampled<=30000?percent:-1;
  return new BatterySnapshot(value,status,plugged,voltage,temperature,valid?now:sampled);
 }
 BatterySnapshot capacity(int value,long now){
  return value>=0&&value<=100?new BatterySnapshot(value,status,plugged,voltage,temperature,now):
    now-sampled<=30000?this:new BatterySnapshot(-1,status,plugged,voltage,temperature,sampled);
 }
 boolean charging(){return status==2||status==5;}
}
