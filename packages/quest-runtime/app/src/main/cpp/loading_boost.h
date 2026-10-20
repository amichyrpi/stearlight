#pragma once
// Share a 20% CPU boost budget between short interactions and longer launches.
// Horizon already boosts the first 45s and owns the actual clocks/thermals.
class LoadingBoost {
 double until=0,last=0,warmup=0,credit=15,continuous=0;
 bool initialized=false,previous=false,wasFocused=false,active=false;
public:
 void request(double now,double seconds){
  if(seconds<=0)return;
  if(seconds>15)seconds=15;
  if(now+seconds>until)until=now+seconds;
 }
 bool update(double now,bool loading,bool focused){
  if(!initialized){initialized=true;last=now;warmup=now+45;}
  double elapsed=now>last?now-last:0;last=now;
  if(wasFocused){
   credit+=elapsed*.2;
   if(active)credit-=elapsed;
   if(credit>15)credit=15;
   if(credit<0)credit=0;
  }
  wasFocused=focused;
  if(!focused){until=0;previous=false;active=false;return false;}
  if(loading&&!previous)request(now,15);
  previous=loading;
  bool wanted=now>=warmup&&now<until;
  // Require a useful amount of credit before restarting, avoiding per-frame
  // boost toggles when a busy UI has exhausted its allowance.
  if(active)active=wanted&&credit>.01&&now-continuous<45;
  else if(wanted&&credit>=1){active=true;continuous=now;}
  return active;
 }
};
