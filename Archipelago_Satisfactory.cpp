#include "Archipelago.h"
#include "Archipelago_Satisfactory.h"

#include <set>
#include <iostream>
#include <json/value.h>
#include <json/writer.h>

//imports from apcpp
extern int ap_player_team;
extern int ap_player_id;
extern std::set<int> teams_set;
extern std::map<int, AP_NetworkPlayer> map_players;
extern Json::FastWriter writer;
extern bool enable_deathlink;

extern std::string getItemName(std::string game, int64_t id);
extern void APSend(std::string req);
//

std::vector<int64_t> all_locations;

void satisfactory_Shutdown() {
    all_locations.clear();
}

int AP_GetCurrentPlayerTeam() {
    return ap_player_team;
}

std::string AP_GetItemName(std::string game, int64_t id) {
    return getItemName(game, id);
}

std::vector<int64_t> AP_GetAllLocations() {
    return all_locations;
}

std::vector<std::pair<int,std::string>> AP_GetAllPlayers() {
    std::vector<std::pair<int,std::string>> allPlayers;

    for (int team : teams_set) {
        for (std::pair<int, AP_NetworkPlayer> player : map_players) {
            std::pair<int,std::string> teamPlayer = std::pair<int,std::string>(team, player.second.alias);

            allPlayers.push_back(teamPlayer);
        }
    }

    return allPlayers;
}

void satisfactory_parse_response(Json::Value& packet, std::string command){
    if (command == "Connected") {
        unsigned int checked_size = packet["checked_locations"].size();
        unsigned int missing_size = packet["missing_locations"].size();

        all_locations.resize(checked_size + missing_size);

        for (unsigned int i = 0; i < checked_size; i++)
            all_locations[i] = packet["checked_locations"][i].asInt64();
        for (unsigned int i = 0; i < missing_size; i++)
            all_locations[checked_size + i] = packet["missing_locations"][i].asInt64();
    }
}

std::function<void(std::string)> log_external = nullptr;
void AP_SetLoggingCallback(std::function<void(std::string)> f_log) {
    log_external = f_log;
}

void log(std::string message) {
    if (log_external)
        log_external(message);
}

// global var wont go out of scope on function boundry end
std::string slot_data;
std::string AP_GetSlotData() {
    AP_GetServerDataRequest request;
    request.key = "_read_slot_data_" + std::to_string(ap_player_id);
    request.value = &slot_data;
    request.type = AP_DataType::Raw;

    AP_GetServerData(&request);

    while (request.status == AP_RequestStatus::Pending && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated) {
        //wait
    }

    if (AP_GetConnectionStatus() != AP_ConnectionStatus::Authenticated){
        log("not connected returning empty string");
        return "";
    } 

    if (request.status != AP_RequestStatus::Done) {
        log("returning empty string as request didnt complete");
        return "";
    } else {
        //slot_data = *(std::string*)request.value;
        return slot_data;
    }
}

void AP_EnabledDeathlinkAnyway() {
    enable_deathlink = false;

    Json::Value setdeathlink;
    setdeathlink["cmd"] = "ConnectUpdate";
    setdeathlink["tags"][0] = "DeathLink";

    std::string request = writer.write(setdeathlink);

    APSend(request);
}