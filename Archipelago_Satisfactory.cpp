#include "Archipelago.h"
#include "Archipelago_Satisfactory.h"

#include <set>

extern int ap_player_team;
extern std::set<int> teams_set;
extern std::map<int, AP_NetworkPlayer> map_players;

extern std::string getItemName(std::string game, int64_t id);

int AP_GetCurrentPlayerTeam() {
    return ap_player_team;
}

std::string AP_GetItemName(std::string game, int64_t id) {
    return getItemName(game, id);
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

