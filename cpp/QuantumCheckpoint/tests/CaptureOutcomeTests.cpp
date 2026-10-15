#include "CaptureOutcome.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace QuantumCheckpoint::capture_outcome;
namespace {
void require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
Outcome prepared(){
 Outcome o;o.attemptId="100:manual:1";o.mainPayloadChecksum="main";o.playerSelected=true;o.layoutKind="field";
 o.qualificationMainChecksum="main";o.qualificationLayoutKind="field";o.qualificationLayoutChecksum="layout";o.handRequiredChecksum="hand";
 o.select(Part::Main,false,"main.json","main");o.select(Part::Hand,true,"hand.json","hand");
 o.select(Part::Qualification,true,"q.json","q");o.select(Part::Layout,true,"field.json","layout");
 return o;
}
void committed(Outcome& o){for(Part p:{Part::Main,Part::Hand,Part::Qualification,Part::Layout})o.written(p);}
}
int main(){
 static_assert(std::is_nothrow_default_constructible_v<Outcome>);
 static_assert(noexcept(std::declval<Outcome&>().failure("","","",false)));
 auto success=prepared();committed(success);
 require(success.player_committed()&&success.all_selected_committed(),"complete selected dependencies");
 require(success.classification()=="player-bundle-committed","success classification");
 for(Part optional:{Part::Spawn,Part::Charge,Part::Turn}){
  auto o=success;o.select(optional,false,"optional.json","opt");o.failed(optional,"IO",false);
  require(o.player_committed()&&!o.all_selected_committed()&&o.classification()=="partial-publication","optional failure retains narrow bundle only");
 }
 for(Part required:{Part::Hand,Part::Qualification,Part::Layout}){
  auto o=prepared();o.written(Part::Main);o.failed(required,"IO",true);o.withdrawn();
  require(!o.player_committed()&&!o.all_selected_committed()&&o.classification()=="partial-publication","required dependency failure");
 }
 auto orphan=prepared();orphan.written(Part::Main);orphan.written(Part::Hand);orphan.written(Part::Qualification);orphan.failed(Part::Layout,"disk",true);orphan.withdrawn();
 require(orphan.components[2].status=="committed"&&orphan.components[3].status=="committed"&&!orphan.player_committed(),"orphans are not deleted or qualified");
 for(int drift=0;drift<4;++drift){auto o=success;
  if(drift==0)o.qualificationMainChecksum="old";if(drift==1)o.qualificationLayoutChecksum="old";
  if(drift==2)o.qualificationLayoutKind="zones";if(drift==3)o.handRequiredChecksum="old";
  require(!o.player_committed()&&o.classification()=="partial-publication","stale binding rejected diagnostically");
 }
 auto first=prepared();first.failure("spawn","IO","first optional",false);first.failure("qualification","unsupported","unknown factory",true);first.failure("layout","dependent","later",true);
 require(first.firstFailure.stage=="spawn"&&first.firstPlayerRefusal.reason=="unknown factory"&&first.events.size()==7,"first failures independent and retained");
 auto before=prepared();before.invalidationStarted=true;before.failure("invalidation","IO","partial deletion unknown",false);
 require(before.classification()=="rejected-before-main"&&!before.invalidationCompleted,"invalidation partial not untouched");
 auto incomplete=success;incomplete.safely([]{throw std::bad_alloc{};});require(!incomplete.player_committed()&&incomplete.classification()=="partial-publication","allocation failure cannot claim qualified");
 auto bounded=success;for(int i=0;i<129;++i)bounded.event("retry","pending","x");require(!bounded.diagnosticComplete&&bounded.events.size()==128,"bounded events fail diagnostic closed");
 auto escaped=success;escaped.failure("q","bad","quote\" slash\\ newline\n tab\t\x01",true);auto text=serialize(escaped);
 require(text.find("quote\\\" slash\\\\ newline\\u000a tab\\u0009\\u0001")!=std::string::npos,"JSON escaping controls");
 bool called=false;require(publish(success,[&](std::string_view payload){called=true;require(payload.find("\"fullPlayerExactRestorationProven\":false")!=std::string::npos,"never full");}),"publisher success");require(called,"writer called");
 require(!publish(success,[](std::string_view){throw std::runtime_error("isolated writer failure");}),"publisher failure isolated");require(success.player_committed(),"publisher failure no mutation");
 auto nohand=prepared();nohand.handRequiredChecksum.clear();nohand.components[2]=Component{};for(Part p:{Part::Main,Part::Qualification,Part::Layout})nohand.written(p);require(nohand.player_committed(),"empty HAND health dependency not fabricated");
 std::cout<<"Capture outcome pure tests passed (diagnostic only; no gameplay).\n";
}
