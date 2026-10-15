#include "CaptureOutcome.hpp"
#include <sstream>
#include <utility>
#include <initializer_list>
namespace QuantumCheckpoint::capture_outcome {
namespace {
constexpr const char* names[]{"main","spawn","hand","qualification","layout","charge","turn"};
std::string quote(std::string_view s) {
    static constexpr char hex[]="0123456789abcdef";
    std::string out{"\""};
    for (unsigned char c:s) {
        if(c=='"'||c=='\\') {out+='\\';out+=static_cast<char>(c);}
        else if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
void failure_json(std::ostringstream& s,const Failure& f) {
    if(f.stage.empty()){s<<"null";return;}
    s<<"{\"stage\":"<<quote(f.stage)<<",\"category\":"<<quote(f.category)<<",\"reason\":"<<quote(f.reason)<<'}';
}
}
void Outcome::event(std::string_view stage,std::string_view category,std::string_view reason) noexcept {
    safely([&]{if(events.size()>=128){diagnosticComplete=false;return;} events.push_back({std::string(stage),std::string(category),std::string(reason)});});
}
void Outcome::failure(std::string_view stage,std::string_view category,std::string_view reason,bool player) noexcept {
    safely([&]{
        Failure f{std::string(stage),std::string(category),std::string(reason)};
        if(firstFailure.stage.empty()) firstFailure=f;
        if(player&&firstPlayerRefusal.stage.empty())firstPlayerRefusal=f;
        if(events.size()<128)events.push_back(std::move(f));else diagnosticComplete=false;
    });
}
void Outcome::select(Part p,bool required,std::string_view path,std::string_view checksum) noexcept {
    safely([&]{auto& c=components[static_cast<unsigned>(p)];c.selected=true;c.requiredForPlayer=required;c.status="prepared";c.path=path;c.payloadChecksum=checksum;});
    event(names[static_cast<unsigned>(p)], "selected", checksum);
}
void Outcome::written(Part p) noexcept {
    safely([&]{components[static_cast<unsigned>(p)].status="committed";});
    event(names[static_cast<unsigned>(p)], "committed", "write_file_atomically returned after MoveFileEx success");
}
void Outcome::failed(Part p,std::string_view reason,bool player) noexcept {
    safely([&]{auto& c=components[static_cast<unsigned>(p)];c.status="failed";c.error=reason;});
    failure(names[static_cast<unsigned>(p)],"publication-failed",reason,player);
}
void Outcome::withdrawn() noexcept {
    safely([&]{for(unsigned i=0;i<components.size();++i) {
        auto& c=components[i];
        if(c.selected&&c.status=="prepared") {
            c.status="withdrawn";
            event(names[i], "withdrawn", "in-memory optional reset; this is not a disk deletion");
        }
    }});
}
bool Outcome::player_committed() const noexcept {
    if(!diagnosticComplete||!playerSelected)return false;
    for(Part p:{Part::Main,Part::Layout,Part::Qualification})if(components[static_cast<unsigned>(p)].status!="committed")return false;
    if(mainPayloadChecksum.empty() || qualificationMainChecksum!=mainPayloadChecksum
       || qualificationLayoutKind!=layoutKind || qualificationLayoutChecksum!=components[static_cast<unsigned>(Part::Layout)].payloadChecksum)
        return false;
    if(!handRequiredChecksum.empty() && handRequiredChecksum!=components[static_cast<unsigned>(Part::Hand)].payloadChecksum)return false;
    for(const auto& c:components)if(c.requiredForPlayer&&(!c.selected||c.status!="committed"))return false;
    return true;
}
bool Outcome::all_selected_committed() const noexcept {
    if(!diagnosticComplete||components[0].status!="committed")return false;
    for(const auto& c:components)if(c.selected&&c.status!="committed")return false;
    return true;
}
std::string_view Outcome::classification() const noexcept {
    if(components[0].status!="committed")return "rejected-before-main";
    if(!diagnosticComplete)return "partial-publication";
    for(const auto& c:components)if(c.selected&&c.status!="committed")return "partial-publication";
    if(playerSelected && !player_committed())return "partial-publication";
    return player_committed()?"player-bundle-committed":"main-only";
}
std::string serialize(const Outcome& o) {
    std::ostringstream s;
    s.exceptions(std::ios::badbit | std::ios::failbit);
    s<<std::boolalpha;
    s<<"{\n\"schemaVersion\":1,\"kind\":\"route-c-capture-outcome\",\"attemptId\":"<<quote(o.attemptId)
     <<",\"origin\":"<<quote(o.origin)<<",\"attemptedAtUtc\":"<<quote(o.attemptedAtUtc)<<",\"finishedAtUtc\":"<<quote(o.finishedAtUtc)
     <<",\"capturedAtUtc\":"<<quote(o.capturedAtUtc)<<",\"mainPayloadChecksum\":"<<quote(o.mainPayloadChecksum)
     <<",\"gameExecutableSha256\":"<<quote(o.executableSha256)<<",\"sourceLevelName\":"<<quote(o.sourceLevelName)<<",\"waveIndex\":"<<o.waveIndex
     <<",\"automaticDeadlineTicks\":"<<o.automaticDeadlineTicks<<",\"layoutKind\":"<<quote(o.layoutKind)<<",\"layoutSchema\":"<<o.layoutSchema<<",\"qualificationPolicy\":"<<quote(o.qualificationPolicy)
     <<",\"qualificationLayoutChecksum\":"<<quote(o.qualificationLayoutChecksum)<<",\"qualificationLayoutKind\":"<<quote(o.qualificationLayoutKind)<<",\"qualificationMainChecksum\":"<<quote(o.qualificationMainChecksum)<<",\"handRequiredChecksum\":"<<quote(o.handRequiredChecksum)
     <<",\"outcome\":"<<quote(o.classification())<<",\"playerBundle\":"<<quote(o.player_committed()?"committed-layout-and-qualification":o.playerSelected?"selected-incomplete":"unavailable")
     <<",\"allSelectedComponentsCommitted\":"<<o.all_selected_committed()<<",\"diagnosticComplete\":"<<o.diagnosticComplete<<",\"fullPlayerExactRestorationProven\":false"
     <<",\"invalidationStarted\":"<<o.invalidationStarted<<",\"invalidationCompleted\":"<<o.invalidationCompleted<<",\"firstFailure\":";
    failure_json(s,o.firstFailure);s<<",\"firstPlayerRefusal\":";failure_json(s,o.firstPlayerRefusal);
    s<<",\"components\":[";
    for(unsigned i=0;i<o.components.size();++i){if(i)s<<',';const auto& c=o.components[i];
        s<<"{\"name\":"<<quote(names[i])<<",\"selected\":"<<c.selected<<",\"requiredForPlayer\":"<<c.requiredForPlayer<<",\"status\":"<<quote(c.status.empty()?"not-selected":c.status)<<",\"path\":"<<quote(c.path)<<",\"payloadChecksum\":"<<quote(c.payloadChecksum)<<",\"sha256\":"<<quote(c.sha256)<<",\"hashStatus\":"<<quote(c.hashStatus.empty()?"not-attempted":c.hashStatus)<<",\"error\":"<<quote(c.error)<<'}';}
    s<<"],\"events\":[";bool first=true;for(const auto& f:o.events){if(!first)s<<',';first=false;failure_json(s,f);}s<<"]}\n";return s.str();
}
bool publish(const Outcome& o,const std::function<void(std::string_view)>& writer) noexcept {
    try {const auto text=serialize(o);writer(text);return true;}catch(...){return false;}
}
}
