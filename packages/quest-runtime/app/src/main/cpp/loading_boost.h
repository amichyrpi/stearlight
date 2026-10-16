#pragma once
// A short loading burst, capped at 15s per 75s (20%). Horizon owns thermal
// throttling and can refuse a boost; sustained gameplay stays SustainedHigh.
class LoadingBoost {
 double until=0, next=0;
 bool previous=false;
public:
 bool update(double now,bool loading,bool focused){
  if(!focused){until=0;previous=false;return false;}
  if(loading&&!previous&&now>=next){until=now+15;next=now+75;}
  previous=loading;
  if(!loading)until=0;
  return now<until;
 }
};
