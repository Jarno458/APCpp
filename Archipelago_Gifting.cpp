#include "Archipelago.h"
#include <chrono>
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
extern std::set<int> teams_set;

// Stuff that is only used for Gifting
std::map<std::pair<int,std::string>,AP_GiftBoxProperties> map_players_to_giftbox;
std::map<std::string, AP_Gift> cur_gifts_available;
bool autoReject = true;
std::chrono::seconds last_giftbox_sync = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());

// PRIV Func Declarations Start
AP_RequestStatus sendGiftInternal(AP_Gift gift);
AP_NetworkPlayer getPlayer(int team, int slot);
AP_NetworkPlayer getPlayer(int team, std::string name);
// PRIV Func Declarations End

#define AP_PLAYER_GIFTBOX_KEY ("GiftBox;" + std::to_string(ap_player_team) + ";" + std::to_string(AP_GetPlayerID()))

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
    map_players_to_giftbox.clear();
    std::map<int,std::string> team_data;
    std::map<int,AP_GetServerDataRequest> team_reqs;
    for (int team : teams_set) {
        team_data[team] = "";
        // Send and wait for data
        team_reqs[team].key = "GiftBoxes;" + std::to_string(team);
        team_reqs[team].type = AP_DataType::Raw;
        team_reqs[team].value = &team_data[team];
        AP_BulkGetServerData(&team_reqs[team]);
    }
    AP_CommitServerData();
    
    while (true && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated) {
        bool done = true;
        for (std::pair<int,AP_GetServerDataRequest> req : team_reqs) {
            if (req.second.status == AP_RequestStatus::Pending) {
                done = false;
                break;
            }
        }
        if (done) break;
    }

    if (AP_GetConnectionStatus() != AP_ConnectionStatus::Authenticated) return map_players_to_giftbox; // Connection Loss occured

    // Write back data if present
    for (int team : teams_set) {
        if (team_reqs[team].status == AP_RequestStatus::Error) continue; // Might just be that noone set it up yet.
        Json::Value json_data;
        reader.parse(team_data[team], json_data);
        for(std::string motherbox_slot : json_data.getMemberNames()) {
            int slot = atoi(motherbox_slot.c_str());
            Json::Value player_global_props = json_data[motherbox_slot];
            AP_NetworkPlayer player = getPlayer(team, slot);
            map_players_to_giftbox[{team,player.name}].IsOpen = player_global_props.get("IsOpen",false).asBool();
            map_players_to_giftbox[{team,player.name}].AcceptsAnyGift = player_global_props.get("AcceptsAnyGift",false).asBool();
            std::vector<std::string> DesiredTraits;
            for (int i = 0; i < player_global_props["DesiredTraits"].size(); i++) {
                DesiredTraits.push_back(player_global_props["DesiredTraits"][i].asString());
            }
            map_players_to_giftbox[{team,player.name}].DesiredTraits = DesiredTraits;
        }
    }
    return map_players_to_giftbox;
}

// Get currently available Gifts in own gift box
std::map<std::string, AP_Gift> AP_CheckGifts() {
    log("AP_CheckGifts() retuning gifts: "+ std::to_string(cur_gifts_available.size()));

    return cur_gifts_available;
}

AP_RequestStatus AP_SendGift(AP_Gift gift) {
    if (gift.IsRefund) return AP_RequestStatus::Error;
    if (map_players_to_giftbox[{gift.ReceiverTeam, gift.Receiver}].IsOpen == true) {
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
    log("AP_AcceptGift(\""+ std::to_string(ids.size()) +"\")");
    //if (cur_gifts_available.count(id)) {
        AP_SetServerDataRequest req;
        req.key = AP_PLAYER_GIFTBOX_KEY;
        req.type = AP_DataType::Raw;
        req.want_reply = true;
        req.operations = std::vector<AP_DataStorageOperation>();
        for (std::string id : ids) {
            std::string json_string = "\"" + id + "\"";
            req.operations.push_back({"pop", &json_string});
            log("AP_AcceptGift() poping off " + json_string);
        }
        AP_SetServerData(&req);
        while (req.status == AP_RequestStatus::Pending && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated) {}
        log("AP_AcceptGift() request status " + std::to_string((int)req.status));
        if (req.status == AP_RequestStatus::Done) {
            // If request is done, there is no need to wait here, the pop either works on the server or the server will disconnect us
            //log("AP_AcceptGift() starting wait loop until id is gone from local vector");
            //while (findGiftByID(id) != -1) {} // When this value is deleted, we can be sure that the gift is no longer there
            //log("AP_AcceptGift() end wait loop");
            log("AP_AcceptGift() result: Done");
            return req.status;
        }
    //}
    log("AP_AcceptGift() result: Error");
    return AP_RequestStatus::Error;
}

AP_RequestStatus AP_RejectGift(std::string id) {
    return AP_RejectGift(std::set<std::string>{ id });
}
AP_RequestStatus AP_RejectGift(std::set<std::string> ids) {
    //if (findGiftByID(id) != -1) {
        std::vector<AP_Gift> gifts;
        std::set<std::string> giftIds;

        std::map<std::string, AP_Gift> availableGiftsCopy = cur_gifts_available;

        for (std::string id : ids) {
            if (availableGiftsCopy.count(id)){
                gifts.push_back(availableGiftsCopy[id]);
                giftIds.insert(id);
            }
        }

        AP_RequestStatus status = AP_AcceptGift(giftIds);
        if (status == AP_RequestStatus::Error) {
            return status;
        }

        bool hasError = false;
        for (AP_Gift gift : gifts) {
            gift.IsRefund = true;
            if (sendGiftInternal(gift) == AP_RequestStatus::Error)
                hasError = true;
        }
    //}
    return (hasError) ? AP_RequestStatus::Error : AP_RequestStatus::Done;
}

void AP_UseGiftAutoReject(bool enable) {
    autoReject = enable;
}

// PRIV
void handleGiftAPISetReply(AP_SetReply reply) {
    log("handleGiftAPISetReply(\""+ reply.key +"\")");

    if (reply.key == AP_PLAYER_GIFTBOX_KEY) {
        log("handleGiftAPISetReply() clearling cur_gifts_available");
        cur_gifts_available.clear();
        Json::Value local_giftbox;
        reader.parse(*(std::string*)reply.value, local_giftbox);
        for (std::string gift_id : local_giftbox.getMemberNames()) {
            AP_Gift gift;
            gift.ID = gift_id;
            gift.ItemName = local_giftbox[gift_id].get("ItemName", "Unknown").asString();
            gift.Amount = local_giftbox[gift_id].get("Amount", 0).asUInt();
            gift.ItemValue = local_giftbox[gift_id].get("ItemValue", 0).asUInt();
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
            cur_gifts_available.emplace(gift_id, gift);
        }
        log("handleGiftAPISetReply() added "+ std::to_string(cur_gifts_available.size()) +" gifs to cur_gifts_available");
        // Perform auto-reject if giftbox closed, or traits do not match
        if (autoReject) {
            std::map<std::string, AP_Gift> gifts = AP_CheckGifts();
            AP_GiftBoxProperties local_box_props = map_players_to_giftbox[{ap_player_team,getPlayer(ap_player_team, AP_GetPlayerID()).name}];
            for (std::pair<std::string, AP_Gift> gift : gifts) {
                if (!local_box_props.IsOpen) {
                    AP_RejectGift(gift.first);
                    continue;
                }
                if (!local_box_props.AcceptsAnyGift) {
                    bool found = false;
                    for (AP_GiftTrait trait_have : gift.second.Traits) {
                        for (std::string trait_want : local_box_props.DesiredTraits) {
                            if (trait_have.Trait == trait_want) {
                                found = true;
                                break;
                            }
                        }
                        if (found) break;
                    }
                    if (!found) AP_RejectGift(gift.first);
                }
            }
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
    std::chrono::seconds time_since_sync = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch() - last_giftbox_sync);
    if (time_since_sync.count() >= 300) {
        AP_QueryGiftBoxes();
    }

    return req.status;
}