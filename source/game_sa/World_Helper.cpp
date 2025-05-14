#include "StdInc.h"

#include "World.h"
#include "EntryExitManager.h"

// inlined
CPlayerInfo& FindPlayerInfo(int32 playerId) {
    return CWorld::Players[playerId < 0 ? CWorld::PlayerInFocus : playerId];
}

// Returns player ped
// 0x56E210
CPlayerPed* FindPlayerPed(int32 playerId) {
    return FindPlayerInfo(playerId).m_pPed;
}

// Returns player vehicle
// 0x56E0D0
CVehicle* FindPlayerVehicle(int32 playerId, bool bIncludeRemote) {
    CPlayerPed* player = FindPlayerPed(playerId);
    if (!player || !player->bInVehicle)
        return nullptr;

    if (bIncludeRemote) {
        CPlayerInfo* playerInfo = player->GetPlayerInfoForThisPlayerPed();
        if (playerInfo->m_pRemoteVehicle) {
            return playerInfo->m_pRemoteVehicle;
        }
    }

    return player->m_pVehicle;
}

// Returns player coors
// 0x56E010
CVector FindPlayerCoors(int32 playerId) {
    CEntity* entity = FindPlayerEntity(playerId);
    return entity ? entity->GetPosition() : CVector{};
}

// Returns player speed
// 0x56E090
CVector& FindPlayerSpeed(int32 playerId) {
    return static_cast<CPhysical*>(FindPlayerEntity(playerId))->m_vecMoveSpeed;
}

// Returns player ped or player vehicle if he's driving
// 0x56E120
CEntity* FindPlayerEntity(int32 playerId) {
    auto player = FindPlayerPed(playerId);
    if (!player)
        return nullptr;

    if (player->IsInVehicle())
        return player->m_pVehicle;

    return player;
}

// Returns player train
// 0x56E160
CTrain* FindPlayerTrain(int32 playerId) {
    auto vehicle = FindPlayerVehicle(playerId);
    if (vehicle && vehicle->IsTrain())
        return vehicle->AsTrain();
    else
        return nullptr;
}

// Returns player coords
// 0x56E250
const CVector& FindPlayerCentreOfWorld(int32 playerId) {
    if (CCarCtrl::bCarsGeneratedAroundCamera)
        return TheCamera.GetPosition();

    if (CVehicle* vehicle = FindPlayerVehicle(playerId, true))
        return vehicle->GetPosition();

    return FindPlayerPed(playerId)->GetPosition();
}

// Returns player coords with skipping sniper shift
// 0x56E320
const CVector& FindPlayerCentreOfWorld_NoSniperShift(int32 playerId) {
    //return plugin::CallAndReturn<const CVector&, 0x56E320, int32>(playerId);

    // TODO: I'm too lazy to look for a corresponding function, originally R* uses a NullVec!
    static const CVector NullVec{ 0.0f, 0.0f, 0.0f };

    // TODO: I had to do this, the compiler keeps giving a warning, sorry I haven't slept yet
    const int32 validPlayerId = playerId;

    if (validPlayerId < 0 || validPlayerId >= MAX_PLAYERS) {
        return NullVec;
    }

    CCamera* cameraTarget = nullptr;

    if (CCarCtrl::bCarsGeneratedAroundCamera) {
        cameraTarget = &TheCamera;
    } else {
        cameraTarget = reinterpret_cast<CCamera*>(CWorld::Players[validPlayerId].m_pRemoteVehicle);

        if (!cameraTarget) {
            int32 effectivePlayerIndex = (validPlayerId >= 0) ? validPlayerId : CWorld::PlayerInFocus;

            if (effectivePlayerIndex < 0 || effectivePlayerIndex >= MAX_PLAYERS) {
                return NullVec;
            }

            auto ped = CWorld::Players[effectivePlayerIndex].m_pPed;

            if (ped && ped->IsInVehicle()) {
                cameraTarget = reinterpret_cast<CCamera*>(ped->m_pVehicle);
            }

            if (!cameraTarget) {
                cameraTarget = reinterpret_cast<CCamera*>(CWorld::Players[validPlayerId].m_pPed);
                if (!cameraTarget) {
                    return NullVec;
                }
            }
        }
    }

    return cameraTarget->m_matrix
        ? cameraTarget->m_matrix->GetPosition()
        : cameraTarget->m_placement.m_vPosn;
}

// Returns player coords with skipping interior shift
// 0x56E400
CVector FindPlayerCentreOfWorld_NoInteriorShift(int32 playerId) {
    return plugin::CallAndReturn<CVector, 0x56E400, int32>(playerId);

    CVector centre = FindPlayerCentreOfWorld(playerId);
    CEntryExitManager::GetPositionRelativeToOutsideWorld(centre);
    return centre;
}

// Returns player angle in radians
// 0x56E450
float FindPlayerHeading(int32 playerId) {
    CVehicle* vehicle = FindPlayerVehicle(playerId, true);
    return vehicle ? vehicle->GetHeading() : FindPlayerPed(playerId)->GetHeading();
}

// Returns Z coord for active player
// NOTSA: Added playerId param
// 0x56E520
float FindPlayerHeight(int32 playerId) {
    return FindPlayerPed(playerId)->GetPosition().z;
}

// Returns player wanted
// 0x56E230
CWanted* FindPlayerWanted(int32 playerId) {
    return FindPlayerInfo(playerId).m_PlayerData.m_pWanted;
}

// Returns player's group
CPedGroup& FindPlayerGroup(int32 playerId) {
    return FindPlayerPed(playerId)->GetPlayerGroup();
}
