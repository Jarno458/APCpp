#include "Archipelago.h"
#include "Archipelago_Satisfactory.h"

#include <set>

extern int ap_player_id;
extern int ap_player_team;
extern std::set<int> teams_set;
extern std::map<int, std::string> map_player_id_alias;

extern std::string getItemName(int64_t id);

int AP_GetCurrentPlayerTeam() {
    return ap_player_team;
}

int AP_GetCurrentPlayerSlot() {
    return ap_player_id;
}

std::string AP_GetItemName(int64_t id) {
    return getItemName(id);
}

std::vector<std::pair<int,std::string>> AP_GetAllPlayers() {
    std::vector<std::pair<int,std::string>> allPlayers;

    for (int team : teams_set) {
        for (std::pair<int, std::string> player : map_player_id_alias) {
            std::pair<int,std::string> teamPlayer = std::pair<int,std::string>(team, player.second);

            allPlayers.push_back(teamPlayer);
        }
    }

    return allPlayers;
}

