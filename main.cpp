#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr int criticalLevel=20, performanceOnBatteryMin=60, chargeNotifyLevel=80, fullNotifyLevel=100, pluggedReminderMinutes=20;
struct PowerState { bool onBattery=false; int capacity=-1; bool valid=false; };
int runProgram(const std::vector<std::string>& args) {
    if (args.empty()) return -1; pid_t pid=fork();
    if (pid==0) { std::vector<char*> argv; for (const auto& a:args) argv.push_back(const_cast<char*>(a.c_str())); argv.push_back(nullptr); execvp(argv[0],argv.data()); _exit(127); }
    if (pid<0) return -1; int status=0; waitpid(pid,&status,0); return WIFEXITED(status)?WEXITSTATUS(status):-1;
}
std::string output(const char* command) {
    std::string result; FILE* pipe=popen(command,"r"); if (!pipe) return result; char buffer[128];
    while (fgets(buffer,sizeof(buffer),pipe)) result+=buffer; pclose(pipe);
    while (!result.empty() && (result.back()=='\n'||result.back()=='\r')) result.pop_back(); return result;
}
class NXPowerDaemon {
public:
    NXPowerDaemon() {
        const char* stateHome=getenv("XDG_STATE_HOME"); const char* home=getenv("HOME");
        stateDir=stateHome&&*stateHome?stateHome:(std::string(home?home:"/tmp")+"/.local/state");
        stateDir+="/nx-powerd"; fs::create_directories(stateDir); debug=std::string(getenv("DEBUG")?getenv("DEBUG"):"0")=="1"; log("start pid="+std::to_string(getpid()));
    }
    void run() {
        PowerState previous=readState();
        if (previous.valid) { applyProfile(previous,false); if (!previous.onBattery) acSince=Clock::now(); } else log("No readable battery found");
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5)); PowerState current=readState(); if (!current.valid) continue;
            bool changed=!previous.valid||current.onBattery!=previous.onBattery, capacityChanged=current.capacity!=previous.capacity;
            if (changed||capacityChanged) { if (changed) transition(previous,current); applyProfile(current,true); notifications(current); }
            else if (!current.onBattery) reminder(current); previous=current;
        }
    }
private:
    std::string battery, stateDir; bool debug=false, chargeHit=false, fullHit=false, criticalHit=false; Clock::time_point acSince=Clock::now();
    void log(const std::string& message) const { if (debug) { std::cout<<message<<'\n'; return; } std::ofstream(stateDir+"/nx-powerd.log",std::ios::app)<<message<<'\n'; }
    bool findBattery() {
        if (!battery.empty()&&fs::exists(battery+"/capacity")) return true; battery.clear(); std::error_code e;
        for (const auto& entry:fs::directory_iterator("/sys/class/power_supply",e))
            if (entry.path().filename().string().rfind("BAT",0)==0&&fs::exists(entry.path()/"capacity")) { battery=entry.path(); return true; }
        return false;
    }
    PowerState readState() {
        PowerState s; if (!findBattery()) { s.valid=true; return s; } std::ifstream cap(battery+"/capacity"), statusFile(battery+"/status"); std::string status;
        if (!(cap>>s.capacity)||!(statusFile>>status)) return s; if (status=="Discharging") s.onBattery=true;
        else if (status!="Charging"&&status!="Full"&&status!="Not charging") return s; s.valid=true; return s;
    }
    void notify(const std::string& urgency,const std::string& icon,const std::string& title,const std::string& body) const {
        if (access("/usr/bin/notify-send",X_OK)!=0&&access("/bin/notify-send",X_OK)!=0) return;
        runProgram({"notify-send","-a","System Message","-t","5000","-u",urgency,"-i",icon,title,body});
    }
    void brightness(int percent) const { if (runProgram({"brightnessctl","set",std::to_string(percent)+"%","--quiet"})==0) log("brightness: "+std::to_string(percent)+"%"); }
    void applyProfile(const PowerState& s,bool notifyChange) {
        std::string target,icon="battery-medium",body; int level=50;
        if (!s.onBattery) { target="performance"; level=65; icon="ac-battery-medium-charging"; body="AC detected: performance"; }
        else if (s.capacity<=criticalLevel) { target="power-saver"; level=20; icon="battery-low"; body="On battery ("+std::to_string(s.capacity)+"%): power-saver"; }
        else if (s.capacity>=performanceOnBatteryMin) { target="performance"; level=65; icon="battery-full"; body="On battery ("+std::to_string(s.capacity)+"%): performance"; }
        else { target="balanced"; body="On battery ("+std::to_string(s.capacity)+"%): balanced"; }
        std::string current=output("powerprofilesctl get 2>/dev/null"); if (current==target) return;
        if (runProgram({"powerprofilesctl","set",target})!=0) return; brightness(level); if (notifyChange) notify(s.onBattery&&s.capacity<=criticalLevel?"critical":"low",icon,"Power Profile",body); log("profile: "+current+" -> "+target+" ("+body+")");
    }
    void transition(const PowerState& oldState,const PowerState& s) {
        if (!s.onBattery&&oldState.onBattery) { criticalHit=false; acSince=Clock::now(); notify("low","ac-adapter","AC Connected","Charging."); log("event: on-ac"); }
        else if (s.onBattery&&!oldState.onBattery) { chargeHit=false; fullHit=false; acSince=Clock::now(); log("event: on-battery"); }
    }
    void notifications(const PowerState& s) {
        if (!s.onBattery) {
            if (!chargeHit&&s.capacity>=chargeNotifyLevel&&s.capacity<fullNotifyLevel) { chargeHit=true; notify("normal","battery","Charge Complete","Battery is at "+std::to_string(s.capacity)+"%. Consider stopping at ~80% for longevity."); }
            if (!fullHit&&s.capacity>=fullNotifyLevel) { fullHit=true; notify("normal","battery-full","Battery Charged","Battery is at "+std::to_string(s.capacity)+"%."); }
        } else if (!criticalHit&&s.capacity<=criticalLevel) { criticalHit=true; notify("critical","battery-low","Battery Critical","Battery is at "+std::to_string(s.capacity)+"%. Please connect AC power."); }
        if (!s.onBattery) reminder(s);
    }
    void reminder(const PowerState& s) {
        if (s.capacity<chargeNotifyLevel) return; auto minutes=std::chrono::duration_cast<std::chrono::minutes>(Clock::now()-acSince).count(); if (minutes<pluggedReminderMinutes) return;
        notify("normal","battery","Battery Care","If you plan to stay on AC, consider enabling a charge limit (if supported) or unplugging occasionally to reduce long-term wear."); acSince=Clock::now(); log("reminder: on-ac pct="+std::to_string(s.capacity));
    }
};
int main() {
    const char* runtime=getenv("XDG_RUNTIME_DIR"); std::string path=std::string(runtime&&*runtime?runtime:"/tmp")+"/nx-powerd.lock"; int fd=open(path.c_str(),O_CREAT|O_RDWR,0600);
    if (fd<0||flock(fd,LOCK_EX|LOCK_NB)<0) return 0; NXPowerDaemon().run();
}
