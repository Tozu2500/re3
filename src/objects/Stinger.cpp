#include "common.h"
#include "Stinger.h"
#include "CopPed.h"
#include "ModelIndices.h"
#include "RpAnimBlend.h"
#include "World.h"
#include "Automobile.h"
#include "Bike.h"
#include "Particle.h"
#include "AnimBlendAssociation.h"
#include "General.h"

uint32 NumOfStingerSegments;

/* --  CStingerSegment  -- */

CStingerSegment::CStingerSegment()
{
	m_fMass = 1.0f;
	m_fTurnMass = 1.0f;
	m_fAirResistance = 0.99999f;
	m_fElasticity = 0.75f;
	m_fBuoyancy = GRAVITY * m_fMass * 0.1f;
	bExplosionProof = true;
	SetModelIndex(MI_PLC_STINGER);
	ObjectCreatedBy = ESCALATOR_OBJECT;
	NumOfStingerSegments++;
}

CStingerSegment::~CStingerSegment()
{
	NumOfStingerSegments--;
}

/* --  CStinger  -- */

CStinger::CStinger()
{
	bIsDeployed = false;
}

void
CStinger::Init(CPed *pPed)
{
	int32 i;

	pOwner = pPed;
	for (i = 0; i < NUM_STINGER_SEGMENTS; i++) {
		pSpikes[i] = new CStingerSegment;
		pSpikes[i]->bUsesCollision = false;
	}
	bIsDeployed = true;
	m_vPos = pPed->GetPosition();
	m_fMax_Z = Atan2(-pPed->GetForward().x, pPed->GetForward().y) + HALFPI;

	for (i = 0; i < NUM_STINGER_SEGMENTS; i++) {
		// shouldn't this be some inlined method? guh...
		CVector pos = pSpikes[i]->GetPosition();
		pSpikes[i]->GetMatrix().SetRotate(0.0f, 0.0f, Atan2(-pPed->GetForward().x, pPed->GetForward().y));
		pSpikes[i]->GetMatrix().Translate(pos);
		pSpikes[i]->SetPosition(m_vPos);
	}

	CVector2D fwd2d(pPed->GetForward().x, pPed->GetForward().y);

	for (i = 0; i < ARRAY_SIZE(m_vPositions); i++)
		m_vPositions[i] = fwd2d * Sin(DEGTORAD(i));

	m_nSpikeState = STINGERSTATE_NONE;
	m_nTimeOfDeploy = CTimer::GetTimeInMilliseconds();
}

void
CStinger::Remove()
{
	if (!bIsDeployed) return;

	for (int32 i = 0; i < NUM_STINGER_SEGMENTS; i++) {
		CStingerSegment *spikeSegment = pSpikes[i];
		if (spikeSegment->m_entryInfoList.first != nil)
			spikeSegment->bRemoveFromWorld = true;
		else
			delete spikeSegment;
	}
	bIsDeployed = false;
}

void
CStinger::Deploy(CPed *pPed)
{
	if (NumOfStingerSegments < NUM_STINGER_SEGMENTS*2 && !pPed->bInVehicle && pPed->IsPedInControl()) {
		if (!bIsDeployed && RpAnimBlendClumpGetAssociation(pPed->GetClump(), ANIM_WEAPON_THROWU) == nil) {
			Init(pPed);
			pPed->SetPedState(PED_DEPLOY_STINGER);
			CAnimManager::AddAnimation(pPed->GetClump(), ASSOCGRP_STD, ANIM_WEAPON_THROWU);
		}
	}
}

void
CStinger::CheckForBurstTyres()
{
	const CVector firstPos = pSpikes[0]->GetPosition();
	const CVector lastPos = pSpikes[NUM_STINGER_SEGMENTS - 1]->GetPosition();
	float dist = (lastPos - firstPos).Magnitude();
	if (dist > 0.1f) return;

	CVehicle *vehsInRange[16];
	int16 numObjects;
	CEntity someEntity;

	CWorld::FindObjectsInRange((lastPos + firstPos) / 2.0f,
		dist, true, &numObjects, 15, (CEntity**)vehsInRange,
		false, true, false, false, false);

	for (int32 i = 0; i < numObjects; i++) {
		CAutomobile *pAutomobile = nil;
		CBike *pBike = nil;

		if (vehsInRange[i]->IsCar())
			pAutomobile = (CAutomobile*)vehsInRange[i];
		else if (vehsInRange[i]->IsBike())
			pBike = (CBike*)vehsInRange[i];

		if (pAutomobile == nil && pBike == nil) continue;

		int wheelId = 0;
		float someWheelDist = sq(((CVehicleModelInfo*)CModelInfo::GetModelInfo(vehsInRange[i]->GetModelIndex()))->m_wheelScale);

		for (; wheelId < 4; wheelId++) {
			if ((pAutomobile != nil && pAutomobile->m_aSuspensionSpringRatioPrev[wheelId] < 1.0f) ||
				(pBike != nil && pBike->m_aSuspensionSpringRatioPrev[wheelId] < 1.0f))
				break;
		}

		if (wheelId >= 4) continue;

		CVector vecWheelPos;
		if (pAutomobile != nil)
			vecWheelPos = pAutomobile->m_aWheelColPoints[wheelId].point;
		else if (pBike != nil)
			vecWheelPos = pBike->m_aWheelColPoints[wheelId].point;

		for (int32 spike = 0; spike < NUM_STINGER_SEGMENTS; spike++) {
			if ((pSpikes[spike]->GetPosition() - vecWheelPos).Magnitude() < someWheelDist) {
				if (pBike) {
					if (wheelId < 2)
						vehsInRange[i]->BurstTyre(CAR_PIECE_WHEEL_LF, true);
					else
						vehsInRange[i]->BurstTyre(CAR_PIECE_WHEEL_LR, true);
				} else {
					switch (wheelId) {
					case 0: vehsInRange[i]->BurstTyre(CAR_PIECE_WHEEL_LF, true); break;
					case 1: vehsInRange[i]->BurstTyre(CAR_PIECE_WHEEL_LR, true); break;
					case 2: vehsInRange[i]->BurstTyre(CAR_PIECE_WHEEL_RF, true); break;
					case 3: vehsInRange[i]->BurstTyre(CAR_PIECE_WHEEL_RR, true); break;
					}
				}
				vecWheelPos.z += 0.15f; // BUG? doesn't that break the burst of other tires?
				for (int j = 0; j < 4; j++)
					CParticle::AddParticle(PARTICLE_BULLETHIT_SMOKE, vecWheelPos, vehsInRange[i]->GetRight() * 0.1f);
			}
		}
	}
}

void
CStinger::Process()
{
	switch (m_nSpikeState)
	{
	case STINGERSTATE_NONE:
		if (pOwner != nil
			&& !pOwner->bInVehicle
			&& pOwner->GetPedState() == PED_DEPLOY_STINGER
			&& RpAnimBlendClumpGetAssociation(pOwner->GetClump(), ANIM_WEAPON_THROWU)->currentTime > 0.39f)
		{
			m_nSpikeState = STINGERSTATE_STATE1;
			for (int i = 0; i < NUM_STINGER_SEGMENTS; i++)
				CWorld::Add(pSpikes[i]);
			pOwner->SetIdle();
		}
		break;
	case STINGERSTATE_STATE2:
		if (pOwner != nil && pOwner->m_nPedType == PEDTYPE_COP)
			((CCopPed*)pOwner)->m_bThrowsSpikeTrap = false;
		break;
	case STINGERSTATE_STATE3:
		if (CTimer::GetTimeInMilliseconds() > m_nTimeOfDeploy + 2500)
			m_nSpikeState = STINGERSTATE_REMOVE;
		// no break
	case STINGERSTATE_STATE1:
		if (m_nSpikeState != STINGERSTATE_STATE1 || CTimer::GetTimeInMilliseconds() <= m_nTimeOfDeploy + 2500) {
			float something = (CTimer::GetTimeInMilliseconds() - m_nTimeOfDeploy) / 2500.0f;
			if (m_nSpikeState != STINGERSTATE_STATE1)
				something = 1.0f - something;

			float radangle = something * ARRAY_SIZE(m_vPositions);
			float angle1 = m_fMax_Z + DEGTORAD(radangle);
			float angle2 = m_fMax_Z - DEGTORAD(radangle);
			int pos = clamp(radangle, 0, ARRAY_SIZE(m_vPositions)-1);

			CVector2D pos2d = m_vPositions[pos];
			CVector pos3d = m_vPos;
			CColPoint colPoint;
			CEntity *pEntity;
			if (CWorld::ProcessVerticalLine(CVector(pos3d.x, pos3d.y, pos3d.z - 10.0f), pos3d.z, colPoint, pEntity, true, false, false, false, true, false, nil))
				pos3d.z = colPoint.point.z + 0.15f;

			angle1 = CGeneral::LimitRadianAngle(angle1);
			angle2 = CGeneral::LimitRadianAngle(angle2);

			for (int spike = 0; spike < NUM_STINGER_SEGMENTS; spike++) {
				CVector somePosAgain = pos3d + CVector(pos2d.x, pos2d.y, 0.6f);
				if (CWorld::TestSphereAgainstWorld(somePosAgain, 0.3f, nil, true, false, false, true, false, false))
					pos2d = CVector2D(0.0f, 0.0f);

				if (spike % 2 == 0) {
					CVector pos = pSpikes[spike]->GetPosition();
					pSpikes[spike]->GetMatrix().SetRotate(0.0f, 0.0f, angle1);
					pSpikes[spike]->GetMatrix().Translate(pos);
					pos3d.x += pos2d.x;
					pos3d.y += pos2d.y;
				} else {
					CVector pos = pSpikes[spike]->GetPosition();
					pSpikes[spike]->GetMatrix().SetRotate(0.0f, 0.0f, angle2);
					pSpikes[spike]->GetMatrix().Translate(pos);
				}
				pSpikes[spike]->SetPosition(pos3d);
			}
		} else
			m_nSpikeState = STINGERSTATE_STATE2;
		break;
	case STINGERSTATE_REMOVE:
		Remove();
		break;
	}
	CheckForBurstTyres();
}