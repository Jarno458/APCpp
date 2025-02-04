#include "Archipelago.h"
#include <json/json.h>
#include <json/value.h>
#include <json/writer.h>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <utility>
#include <vector>

extern Json::FastWriter writer;
extern Json::Reader reader;
extern std::mt19937_64 rando;
extern int ap_player_team;
extern int ap_player_id;
extern std::set<int> teams_set;

extern void log(std::string message);

#define AP_PLAYER_GIFTBOX_KEY ("GiftBox;" + std::to_string(ap_player_team) + ";" + std::to_string(ap_player_id))

// Stuff that is only used for Gifting
std::map<std::pair<int,std::string>,AP_GiftBoxProperties> map_players_to_giftbox;
std::vector<AP_Gift> cur_gifts_available;
bool autoReject = true;

// PRIV Func Declarations Start
AP_RequestStatus sendGiftInternal(AP_Gift gift);
AP_NetworkPlayer getPlayer(int team, int slot);
AP_NetworkPlayer getPlayer(int team, std::string name);
// PRIV Func Declarations End

AP_RequestStatus AP_SetGiftBoxProperties(AP_GiftBoxProperties props) {
    // Create Local Box if needed
    AP_SetServerDataRequest req_local_box;
    req_local_box.key = AP_PLAYER_GIFTBOX_KEY;
    std::string LocalGiftBoxDef_s = writer.write(Json::objectValue);
    req_local_box.default_value = &LocalGiftBoxDef_s;
    req_local_box.operations = {{"default", &LocalGiftBoxDef_s}};
    req_local_box.type = AP_DataType::Raw;
    req_local_box.want_reply = true;
    AP_BulkSetServerData(&req_local_box);

    // Set Properties
    Json::Value GlobalGiftBox = Json::objectValue;
    GlobalGiftBox[std::to_string(AP_GetPlayerID())]["IsOpen"] = props.IsOpen;
    GlobalGiftBox[std::to_string(AP_GetPlayerID())]["AcceptsAnyGift"] = props.AcceptsAnyGift;
    GlobalGiftBox[std::to_string(AP_GetPlayerID())]["DesiredTraits"] = Json::arrayValue;
    for (std::string trait_s : props.DesiredTraits)
        GlobalGiftBox[std::to_string(AP_GetPlayerID())]["DesiredTraits"].append(trait_s);
    GlobalGiftBox[std::to_string(AP_GetPlayerID())]["MinimumGiftDataVersion"] = 2;
    GlobalGiftBox[std::to_string(AP_GetPlayerID())]["MaximumGiftDataVersion"] = 2;

    // Update entry in MotherBox
    AP_SetServerDataRequest req_global_box;
    req_global_box.key = "GiftBoxes;" + std::to_string(ap_player_team);
    std::string GlobalGiftBox_s = writer.write(GlobalGiftBox);
    Json::Value DefBoxGlobal;
    DefBoxGlobal[std::to_string(AP_GetPlayerID())] = Json::objectValue;
    std::string DefBoxGlobal_s = writer.write(DefBoxGlobal);
    req_global_box.operations = {
        {"default", &DefBoxGlobal_s},
        {"update", &GlobalGiftBox_s}};
    req_global_box.default_value = &DefBoxGlobal_s;
    req_global_box.type = AP_DataType::Raw;
    req_global_box.want_reply = false;
    AP_BulkSetServerData(&req_global_box);

    // Set Values
    AP_CommitServerData();
    while (req_local_box.status == AP_RequestStatus::Pending && req_global_box.status == AP_RequestStatus::Pending && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated) {}
    if (req_global_box.status != AP_RequestStatus::Done || req_local_box.status != AP_RequestStatus::Done)
        return AP_RequestStatus::Error;
    return AP_RequestStatus::Done;
}

std::map<std::pair<int,std::string>,AP_GiftBoxProperties> AP_QueryGiftBoxes() {
    return map_players_to_giftbox;
}

// Get currently available Gifts in own gift box
std::vector<AP_Gift> AP_CheckGifts() {
    return cur_gifts_available;
}

AP_RequestStatus AP_SendGift(AP_Gift gift) {
    if (gift.IsRefund) return AP_RequestStatus::Error;

    std::pair<int,std::string> key = {gift.ReceiverTeam, gift.Receiver};

    if (map_players_to_giftbox.count(key) && map_players_to_giftbox[key].IsOpen == true) {
        gift.SenderTeam = ap_player_team;
        gift.Sender = AP_GetPlayerID();
        return sendGiftInternal(gift);
    }
    return AP_RequestStatus::Error;
}

AP_RequestStatus AP_AcceptGift(std::string id) {
    return AP_AcceptGift(std::set<std::string>{ id });
}
AP_RequestStatus AP_AcceptGift(std::set<std::string> ids) {
   if (ids.size() == 0)
        return AP_RequestStatus::Done;

    AP_SetServerDataRequest req;
    req.key = AP_PLAYER_GIFTBOX_KEY;
    req.type = AP_DataType::Raw;
    req.want_reply = true;
    req.operations = std::vector<AP_DataStorageOperation>();

    std::vector<std::string> id_strings;
    for (std::string id : ids) 
        id_strings.emplace_back("\"" + id + "\"");
    for (int i=0;i < id_strings.size(); i++)
        req.operations.push_back({"pop", &id_strings[i]});
    
    AP_SetServerData(&req);
    while (req.status == AP_RequestStatus::Pending && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated) {}
    return req.status;
}

AP_RequestStatus AP_RejectGift(std::string id) {
    return AP_RejectGift(std::set<std::string>{ id });
}
AP_RequestStatus AP_RejectGift(std::set<std::string> ids) {
    std::set<AP_Gift*> gifts;
    std::set<std::string> giftIds;
    std::set<std::string> missingGiftIds;
    std::vector<AP_Gift> availableGiftsCopy = AP_CheckGifts();

    for (std::string id : ids) {
        bool found = false;

        for (AP_Gift& gift : availableGiftsCopy){
            if (!found && gift.ID == id){
                found = true;
                gifts.insert(&gift);
                giftIds.insert(gift.ID);
            }
        }

        if (!found)
            missingGiftIds.insert(id);
    }

    AP_RequestStatus status = AP_AcceptGift(giftIds);
    if (missingGiftIds.empty() || status == AP_RequestStatus::Error) {
        return AP_RequestStatus::Error;
    }

    bool hasError = false;
    for (AP_Gift* gift : gifts) {
        gift->IsRefund = true;
        if (sendGiftInternal(*gift) == AP_RequestStatus::Error)
            hasError = true;
    }

    return (hasError) ? AP_RequestStatus::Error : AP_RequestStatus::Done;
}

void AP_UseGiftAutoReject(bool enable) {
    autoReject = enable;
}

// PRIV
void handleGiftAPISetReply(AP_SetReply reply) {
    log("handleGiftAPISetReply");

    if (reply.key == AP_PLAYER_GIFTBOX_KEY) {
        cur_gifts_available.clear();
        Json::Value local_giftbox;
        reader.parse(*(std::string*)reply.value, local_giftbox);
        for (std::string gift_id : local_giftbox.getMemberNames()) {
            AP_Gift gift;
            gift.ID = gift_id;
            gift.ItemName = local_giftbox[gift_id].get("ItemName", "Unknown").asString();
            gift.Amount = local_giftbox[gift_id].get("Amount", 0).asUInt();
            gift.ItemValue = local_giftbox[gift_id].get("ItemValue", 0).asInt64();
            for (Json::Value trait_v : local_giftbox[gift_id]["Traits"]) {
                AP_GiftTrait trait;
                trait.Trait = trait_v.get("Trait", "Unknown").asString();
                trait.Quality = trait_v.get("Quality", 1.).asDouble();
                trait.Duration = trait_v.get("Duration", 1.).asDouble();
                gift.Traits.push_back(trait);
            }
            AP_NetworkPlayer SenderPlayer = getPlayer(local_giftbox[gift_id].get("SenderTeam", 0).asInt(), local_giftbox[gift_id]["SenderSlot"].asInt());
            AP_NetworkPlayer ReceiverPlayer = getPlayer(local_giftbox[gift_id].get("ReceiverTeam", 0).asInt(), local_giftbox[gift_id]["ReceiverSlot"].asInt());
            gift.Sender = SenderPlayer.name;
            gift.Receiver = ReceiverPlayer.name;
            gift.SenderTeam = SenderPlayer.team;
            gift.ReceiverTeam = ReceiverPlayer.team;
            gift.IsRefund = local_giftbox[gift_id].get("IsRefund", false).asBool();
            cur_gifts_available.push_back(gift);
        }
        // Perform auto-reject if giftbox closed, or traits do not match
        if (autoReject) {
            std::vector<AP_Gift> availableGiftsCopy = AP_CheckGifts();
            AP_GiftBoxProperties local_box_props = map_players_to_giftbox[{ap_player_team,getPlayer(ap_player_team, AP_GetPlayerID()).name}];
            std::set<std::string> giftIdsToReject;
            for (AP_Gift& gift : availableGiftsCopy) {
                if (!local_box_props.IsOpen) {
                    giftIdsToReject.insert(gift.ID);
                    continue;
                }
                if (!local_box_props.AcceptsAnyGift) {
                    bool found = false;
                    for (AP_GiftTrait trait_have : gift.Traits) {
                        for (std::string trait_want : local_box_props.DesiredTraits) {
                            if (trait_have.Trait == trait_want) {
                                found = true;
                                break;
                            }
                        }
                        if (found) break;
                    }
                    if (!found) {
                        giftIdsToReject.insert(gift.ID);
                    }
                }

                AP_RejectGift(giftIdsToReject);
            }
        }
    } else if (reply.key.rfind("GiftBoxes;", 0) == 0) {
        int team = stoi(reply.key.substr(10)); //10 = length of "GiftBoxes;"
        Json::Value json_data;
        reader.parse(*(std::string*)reply.value, json_data);
        std::set<std::string> foundPlayersNames;
        for(std::string motherbox_slot : json_data.getMemberNames()) {
            int slot = atoi(motherbox_slot.c_str());
            Json::Value player_global_props = json_data[motherbox_slot];
            AP_NetworkPlayer player = getPlayer(team, slot);
            foundPlayersNames.insert(player.name);
            map_players_to_giftbox[{team,player.name}].IsOpen = player_global_props.get("IsOpen",false).asBool();
            map_players_to_giftbox[{team,player.name}].AcceptsAnyGift = player_global_props.get("AcceptsAnyGift",false).asBool();
            std::vector<std::string> DesiredTraits;
            for (unsigned int i = 0; i < player_global_props["DesiredTraits"].size(); i++) {
                DesiredTraits.push_back(player_global_props["DesiredTraits"][i].asString());
            }
            map_players_to_giftbox[{team,player.name}].DesiredTraits = DesiredTraits;
        }
        //remove non existing entries
        std::vector<std::pair<int,std::string>> keysToRemove;
        for(std::pair<const std::pair<int,std::string>,AP_GiftBoxProperties>& motherboxData : map_players_to_giftbox) {
            if (motherboxData.first.first == team && foundPlayersNames.find(motherboxData.first.second) == foundPlayersNames.end()){
                keysToRemove.push_back(motherboxData.first);
            }
        }
        for(std::pair<int,std::string>& keyToRemove : keysToRemove) {
            map_players_to_giftbox.erase(keyToRemove);
        }
    }
}

AP_RequestStatus sendGiftInternal(AP_Gift gift) {
    if (gift.IsRefund && gift.Sender == getPlayer(ap_player_team, AP_GetPlayerID()).name) {
        // Loop detected! Rejecting a gift to yourself means you dont want what is in here. Should be safe to return success immediately
        return AP_RequestStatus::Done;
    }
    std::string giftbox_key = "GiftBox;";
    AP_NetworkPlayer SenderPlayer = getPlayer(gift.SenderTeam, gift.Sender);
    AP_NetworkPlayer ReceiverPlayer = getPlayer(gift.ReceiverTeam, gift.Receiver);
    if (!gift.IsRefund)
        giftbox_key += std::to_string(gift.ReceiverTeam) + ";" + std::to_string(ReceiverPlayer.slot);
    else
        giftbox_key += std::to_string(gift.SenderTeam) + ";" + std::to_string(SenderPlayer.slot);

    Json::Value giftVal;
    if (gift.IsRefund) {
        giftVal["ID"] = gift.ID;
    } else {
        uint64_t random1 = rando();
        uint64_t random2 = rando();
        std::ostringstream id;
        id << std::hex << random1 << random2;
        giftVal["ID"] = id.str();
    }
    giftVal["ItemName"] = gift.ItemName;
    giftVal["Amount"] = gift.Amount;
    giftVal["ItemValue"] = gift.ItemValue;
    for (AP_GiftTrait trait : gift.Traits) {
        Json::Value trait_v;
        trait_v["Trait"] = trait.Trait;
        trait_v["Quality"] = trait.Quality;
        trait_v["Duration"] = trait.Duration;
        giftVal["Traits"].append(trait_v);
    }
    giftVal["SenderSlot"] = SenderPlayer.slot;
    giftVal["ReceiverSlot"] = ReceiverPlayer.slot;
    giftVal["SenderTeam"] = gift.SenderTeam;
    giftVal["ReceiverTeam"] = gift.ReceiverTeam;
    giftVal["IsRefund"] = gift.IsRefund;
    Json::Value player_box_update;
    player_box_update[giftVal["ID"].asString()] = giftVal;
    std::string gift_s = writer.write(player_box_update);

    AP_SetServerDataRequest req;
    req.key = giftbox_key;
    req.type = AP_DataType::Raw;
    req.want_reply = true;
    Json::Value defVal = Json::objectValue;
    std::string defVal_s = writer.write(defVal);
    req.default_value = &defVal_s;

    req.operations = {
        {"default", &defVal_s},
        {"update", &gift_s}
    };

    AP_SetServerData(&req);
    while (req.status == AP_RequestStatus::Pending && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated) {}
    return req.status;
}