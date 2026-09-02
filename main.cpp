// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux Latinoamericana S.C. <hello@nxos.org>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
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
constexpr int defaultPowerSaverMax=20, defaultBalancedMax=59, defaultPerformanceMin=60, chargeNotifyLevel=80, fullNotifyLevel=100, pluggedReminderMinutes=20;
struct PowerState { bool onBattery=false; int capacity=-1; bool valid=false; };
struct PowerConfig { bool enabled=true; int powerSaverMax=defaultPowerSaverMax; int balancedMax=defaultBalancedMax; int performanceMin=defaultPerformanceMin; };
int runProgram(const std::vector<std::string>& args) {
    if (args.empty())
        return -1;
    pid_t pid = fork();
    if (pid==0) { std::vector<char*> argv; for (const auto& a:args) argv.push_back(const_cast<char*>(a.c_str())); argv.push_back(nullptr); execvp(argv[0],argv.data()); _exit(127); }
    if (pid < 0)
        return -1;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
std::string output(const char* command) {
    std::string result; FILE* pipe=popen(command,"r"); if (!pipe) return result; char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe))
        result += buffer;
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}
class NXPowerDaemon {
public:
    NXPowerDaemon() {
        const char* stateHome=getenv("XDG_STATE_HOME"); const char* home=getenv("HOME");
        stateDir=stateHome&&*stateHome?stateHome:(std::string(home?home:"/tmp")+"/.local/state");
        stateDir += "/nx-powerd";
        fs::create_directories(stateDir);
        configPath = std::string(home && *home ? home : "/tmp") + "/.config/nx-powerd/nx-powerd.conf";
        ensureConfig();
        debug = std::string(getenv("DEBUG") ? getenv("DEBUG") : "0") == "1";
        log("start pid=" + std::to_string(getpid()));
    }
    void run() {
        PowerConfig config=readConfig(); PowerState previous=readState();
        if (previous.valid) { if (config.enabled) applyProfile(previous,config,false); if (!previous.onBattery) acSince=Clock::now(); } else log("No readable battery found");
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5)); config=readConfig(); PowerState current=readState(); if (!current.valid) continue;
            bool changed=!previous.valid||current.onBattery!=previous.onBattery, capacityChanged=current.capacity!=previous.capacity;
            if (changed||capacityChanged) { if (changed) transition(previous,current); if (config.enabled) applyProfile(current,config,true); notifications(current); }
            else if (!current.onBattery)
                reminder(current);
            previous = current;
        }
    }
private:
    std::string battery, stateDir, configPath; bool debug=false, chargeHit=false, fullHit=false, criticalHit=false; Clock::time_point acSince=Clock::now();
    void log(const std::string& message) const { if (debug) { std::cout<<message<<'\n'; return; } std::ofstream(stateDir+"/nx-powerd.log",std::ios::app)<<message<<'\n'; }
    bool findBattery() {
        if (!battery.empty() && fs::exists(battery + "/capacity"))
            return true;
        battery.clear();
        std::error_code e;
        for (const auto& entry:fs::directory_iterator("/sys/class/power_supply",e))
            if (entry.path().filename().string().rfind("BAT",0)==0&&fs::exists(entry.path()/"capacity")) { battery=entry.path(); return true; }
        return false;
    }
    PowerState readState() {
        PowerState s; if (!findBattery()) { s.valid=true; return s; } std::ifstream cap(battery+"/capacity"), statusFile(battery+"/status"); std::string status;
        if (!(cap >> s.capacity) || !(statusFile >> status))
            return s;
        if (status == "Discharging")
            s.onBattery = true;
        else if (status != "Charging" && status != "Full" && status != "Not charging")
            return s;
        s.valid = true;
        return s;
    }
    void ensureConfig() const
    {
        if (fs::exists(configPath))
            return;

        std::error_code error;
        fs::create_directories(fs::path(configPath).parent_path(), error);
        if (error) {
            log("Could not create config directory: " + error.message());
            return;
        }

        std::ofstream config(configPath);
        if (!config) {
            log("Could not create config file: " + configPath);
            return;
        }

        config << "[Daemon]\n"
               << "enabled=true\n\n"
               << "[Profiles]\n"
               << "powerSaverMax=" << defaultPowerSaverMax << "\n"
               << "balancedMax=" << defaultBalancedMax << "\n"
               << "performanceMin=" << defaultPerformanceMin << "\n";
    }
    void notify(const std::string& urgency,const std::string& icon,const std::string& title,const std::string& body) const {
        if (access("/usr/bin/notify-send",X_OK)!=0&&access("/bin/notify-send",X_OK)!=0) return;
        runProgram({"notify-send","-a","System Message","-t","5000","-u",urgency,"-i",icon,title,body});
    }
    void brightness(int percent) const { if (runProgram({"brightnessctl","set",std::to_string(percent)+"%","--quiet"})==0) log("brightness: "+std::to_string(percent)+"%"); }
    static std::string trim(std::string value) { const auto first=value.find_first_not_of(" \t\r\n"), last=value.find_last_not_of(" \t\r\n"); return first==std::string::npos?std::string{}:value.substr(first,last-first+1); }
    PowerConfig readConfig() const { PowerConfig config; std::ifstream file(configPath); std::string line, section; while (std::getline(file,line)) { line=trim(line); if (line.empty()||line[0]=="#"[0]||line[0]==";"[0]) continue; if (line.front()=="["[0]&&line.back()=="]"[0]) { section=trim(line.substr(1,line.size()-2)); continue; } const auto separator=line.find("="[0]); if (separator==std::string::npos) continue; const auto key=trim(line.substr(0,separator)), value=trim(line.substr(separator+1)); if (section=="Daemon"&&key=="enabled") config.enabled=value=="true"||value=="1"||value=="yes"||value=="on"; if (section=="Profiles") { try { const int number=std::stoi(value); if (key=="powerSaverMax") config.powerSaverMax=number; if (key=="balancedMax") config.balancedMax=number; if (key=="performanceMin") config.performanceMin=number; } catch (...) {} } } config.powerSaverMax=std::clamp(config.powerSaverMax,0,98); config.balancedMax=std::clamp(config.balancedMax,config.powerSaverMax+1,99); config.performanceMin=std::clamp(config.performanceMin,config.balancedMax+1,100); return config; }
    void applyProfile(const PowerState& s,const PowerConfig& config,bool notifyChange) {
        std::string target,icon="battery-medium",body; int level=50;
        if (!s.onBattery) { target="performance"; level=65; icon="battery-medium-charging"; body="AC detected: performance"; }
        else if (s.capacity<=config.powerSaverMax) { target="power-saver"; level=20; icon="battery-low"; body="On battery ("+std::to_string(s.capacity)+"%): power-saver"; }
        else if (s.capacity>=config.performanceMin) { target="performance"; level=65; icon="battery-full"; body="On battery ("+std::to_string(s.capacity)+"%): performance"; }
        else { target="balanced"; body="On battery ("+std::to_string(s.capacity)+"%): balanced"; }
        std::string current=output("powerprofilesctl get 2>/dev/null"); if (current==target) return;
        if (runProgram({"powerprofilesctl", "set", target}) != 0)
            return;
        brightness(level);
        if (notifyChange)
            notify(s.onBattery && s.capacity <= config.powerSaverMax ? "critical" : "low", icon, "Power Profile", body);
        log("profile: " + current + " -> " + target + " (" + body + ")");
    }
    void transition(const PowerState& oldState,const PowerState& s) {
        if (!s.onBattery&&oldState.onBattery) { criticalHit=false; acSince=Clock::now(); notify("low","battery-good-charging","AC Connected","Charging."); log("event: on-ac"); }
        else if (s.onBattery&&!oldState.onBattery) { chargeHit=false; fullHit=false; acSince=Clock::now(); log("event: on-battery"); }
    }
    void notifications(const PowerState& s) {
        if (!s.onBattery) {
            if (!chargeHit&&s.capacity>=chargeNotifyLevel&&s.capacity<fullNotifyLevel) { chargeHit=true; notify("normal","battery-full","Charge Complete","Battery is at "+std::to_string(s.capacity)+"%. Consider stopping at ~80% for longevity."); }
            if (!fullHit&&s.capacity>=fullNotifyLevel) { fullHit=true; notify("normal","battery-full","Battery Charged","Battery is at "+std::to_string(s.capacity)+"%."); }
        } else if (!criticalHit&&s.capacity<=defaultPowerSaverMax) { criticalHit=true; notify("critical","battery-low","Battery Critical","Battery is at "+std::to_string(s.capacity)+"%. Please connect AC power."); }
        if (!s.onBattery) reminder(s);
    }
    void reminder(const PowerState& s) {
        if (s.capacity < chargeNotifyLevel)
            return;
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(Clock::now() - acSince).count();
        if (minutes < pluggedReminderMinutes)
            return;
        notify("normal","battery-full","Battery Care","If you plan to stay on AC, consider enabling a charge limit (if supported) or unplugging occasionally to reduce long-term wear."); acSince=Clock::now(); log("reminder: on-ac pct="+std::to_string(s.capacity));
    }
};
int main() {
    const char* runtime=getenv("XDG_RUNTIME_DIR"); std::string path=std::string(runtime&&*runtime?runtime:"/tmp")+"/nx-powerd.lock"; int fd=open(path.c_str(),O_CREAT|O_RDWR,0600);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) < 0)
        return 0;
    NXPowerDaemon().run();
}
