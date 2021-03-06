#include "playerdefinition.h"

#include <assert.h>
#include <string.h>
#include <algorithm>

#include <prism/file.h>
#include <prism/physicshandler.h>
#include <prism/log.h>
#include <prism/system.h>
#include <prism/timer.h>
#include <prism/math.h>
#include <prism/mugendefreader.h>
#include <prism/mugenanimationreader.h>
#include <prism/mugenanimationhandler.h>
#include <prism/mugentexthandler.h>
#include <prism/screeneffect.h>
#include <prism/input.h>
#include <prism/soundeffect.h>

#include "mugencommandreader.h"
#include "mugenstatereader.h"
#include "mugencommandhandler.h"
#include "mugenstatehandler.h"
#include "playerhitdata.h"
#include "projectile.h"
#include "mugenlog.h"

#include "stage.h"
#include "fightui.h"
#include "mugenstagehandler.h"
#include "gamelogic.h"
#include "ai.h"
#include "collision.h"
#include "mugensound.h"
#include "mugenanimationutilities.h"
#include "mugenexplod.h"
#include "pausecontrollers.h"
#include "config.h"
#include "mugenassignmentevaluator.h"

using namespace std;

#define SHADOW_Z 32
#define REFLECTION_Z 33
#define DUST_Z 47
#define WIDTH_LINE_Z 48
#define CENTER_POINT_Z 49
#define PLAYER_DEBUG_TEXT_Z 79

static struct {
	DreamPlayerHeader mPlayerHeader[2];
	DreamPlayer mPlayers[2];
	int mUniqueIDCounter;
	int mIsInTrainingMode;
	int mIsCollisionDebugActive;
	MemoryStack* mMemoryStack;
	int mIsLoading;
	int mHasLoadedSprites;

	List mAllPlayers; // contains DreamPlayer
	std::map<int, DreamPlayer> mHelperStore;

	double mTimeDilatationNow;
	int mTimeDilatationUpdates;
	double mTimeDilatation;
} gPlayerDefinition;

static void loadPlayerHeaderFromScript(DreamPlayerHeader* tHeader, MugenDefScript* tScript) {
	getMugenDefStringOrDefault(tHeader->mConstants.mName, tScript, "Info", "name", "Character");
	getMugenDefStringOrDefault(tHeader->mConstants.mDisplayName, tScript, "Info", "displayname", tHeader->mConstants.mName);
	getMugenDefStringOrDefault(tHeader->mConstants.mVersion, tScript, "Info", "versiondate", "09,09,2017");
	getMugenDefStringOrDefault(tHeader->mConstants.mMugenVersion, tScript, "Info", "mugenversion", "1.1");
	getMugenDefStringOrDefault(tHeader->mConstants.mAuthor, tScript, "Info", "author", "John Doe");
	getMugenDefStringOrDefault(tHeader->mConstants.mPaletteDefaults, tScript, "Info", "pal.defaults", "1");

	tHeader->mConstants.mLocalCoordinates = getMugenDefVectorIOrDefault(tScript, "Info", "localcoord", makeVector3DI(320, 240, 0));
	if (!tHeader->mConstants.mLocalCoordinates.x) tHeader->mConstants.mLocalCoordinates.x = 320;
	if (!tHeader->mConstants.mLocalCoordinates.y) tHeader->mConstants.mLocalCoordinates.y = 240;
}

static void loadOptionalStateFiles(MugenDefScript* tScript, char* tPath, DreamPlayer* tPlayer) {
	char file[200];
	char scriptPath[1024];
	char name[100];

	int i;
	for (i = 0; i < 100; i++) {
		sprintf(name, "st%d", i);
		getMugenDefStringOrDefault(file, tScript, "Files", name, "");
		sprintf(scriptPath, "%s%s", tPath, file);
		if (!isFile(scriptPath)) continue;
		
		loadDreamMugenStateDefinitionsFromFile(&tPlayer->mHeader->mFiles.mConstants.mStates, scriptPath);
		logMemoryPlatform();
	}

	
}

static void setPlayerFaceDirection(DreamPlayer* p, FaceDirection tDirection);

static void setPlayerExternalDependencies(DreamPlayer* tPlayer) {
	tPlayer->mPhysicsElement = addToPhysicsHandler(getDreamPlayerStartingPosition(tPlayer->mRootID, tPlayer->mHeader->mConstants.mLocalCoordinates.y));
	setPlayerPhysics(tPlayer, MUGEN_STATE_PHYSICS_STANDING);
	setPlayerStateMoveType(tPlayer, MUGEN_STATE_MOVE_TYPE_IDLE);
	setPlayerStateType(tPlayer, MUGEN_STATE_TYPE_STANDING);

	Position p = getDreamStageCoordinateSystemOffset(getPlayerCoordinateP(tPlayer));
	p.z = PLAYER_Z;
	tPlayer->mActiveAnimations = &tPlayer->mHeader->mFiles.mAnimations;
	tPlayer->mAnimationElement = addMugenAnimation(getMugenAnimation(&tPlayer->mHeader->mFiles.mAnimations, 0), gPlayerDefinition.mIsLoading ? NULL : &tPlayer->mHeader->mFiles.mSprites, p);
	setMugenAnimationDrawScale(tPlayer->mAnimationElement, tPlayer->mHeader->mFiles.mConstants.mSizeData.mScale);
	setMugenAnimationBasePosition(tPlayer->mAnimationElement, getHandledPhysicsPositionReference(tPlayer->mPhysicsElement));
	setMugenAnimationCameraPositionReference(tPlayer->mAnimationElement, getDreamMugenStageHandlerCameraPositionReference());
	setMugenAnimationAttackCollisionActive(tPlayer->mAnimationElement, getDreamPlayerAttackCollisionList(tPlayer), NULL, NULL, getPlayerHitDataReference(tPlayer));
	setMugenAnimationPassiveCollisionActive(tPlayer->mAnimationElement, getDreamPlayerPassiveCollisionList(tPlayer), playerHitCB, tPlayer, getPlayerHitDataReference(tPlayer));
	tPlayer->mStateMachineID = registerDreamMugenStateMachine(&tPlayer->mHeader->mFiles.mConstants.mStates, tPlayer);
}

static void loadPlayerFiles(char* tPath, DreamPlayer* tPlayer, MugenDefScript* tScript) {
	char file[200];
	char path[1024];
	char scriptPath[1024];
	char name[100];
	getPathToFile(path, tPath);

	getMugenDefStringOrDefault(file, tScript, "Files", "cmd", "");
	assert(strcmp("", file));
	sprintf(scriptPath, "%s%s", path, file);
	tPlayer->mHeader->mFiles.mCommands = loadDreamMugenCommandFile(scriptPath);
	logMemoryPlatform();
	tPlayer->mCommandID = registerDreamMugenCommands(tPlayer->mControllerID, &tPlayer->mHeader->mFiles.mCommands);
	logMemoryPlatform();

	setDreamAssignmentCommandLookupID(tPlayer->mCommandID);
	getMugenDefStringOrDefault(file, tScript, "Files", "cns", "");
	assert(strcmp("", file));
	sprintf(scriptPath, "%s%s", path, file);
	tPlayer->mHeader->mFiles.mConstants = loadDreamMugenConstantsFile(scriptPath);
	logMemoryPlatform();
	
	getMugenDefStringOrDefault(file, tScript, "Files", "stcommon", "");
	sprintf(scriptPath, "%s%s", path, file);
	if (isFile(scriptPath)) {
		loadDreamMugenStateDefinitionsFromFile(&tPlayer->mHeader->mFiles.mConstants.mStates, scriptPath);
	}
	else {
		sprintf(scriptPath, "assets/data/%s", file);
		if (isFile(scriptPath)) {
			loadDreamMugenStateDefinitionsFromFile(&tPlayer->mHeader->mFiles.mConstants.mStates, scriptPath);
		}
	}
	logMemoryPlatform();

	getMugenDefStringOrDefault(file, tScript, "Files", "st", "");
	sprintf(scriptPath, "%s%s", path, file);
	if (isFile(scriptPath)) {
		loadDreamMugenStateDefinitionsFromFile(&tPlayer->mHeader->mFiles.mConstants.mStates, scriptPath);
	}
	logMemoryPlatform();

	loadOptionalStateFiles(tScript, path, tPlayer);

	getMugenDefStringOrDefault(file, tScript, "Files", "cmd", "");
	assert(strcmp("", file));
	sprintf(scriptPath, "%s%s", path, file);
	loadDreamMugenStateDefinitionsFromFile(&tPlayer->mHeader->mFiles.mConstants.mStates, scriptPath);

	resetDreamAssignmentCommandLookupID();

	getMugenDefStringOrDefault(file, tScript, "Files", "anim", "");
	assert(strcmp("", file));
	sprintf(scriptPath, "%s%s", path, file);
	tPlayer->mHeader->mFiles.mAnimations = loadMugenAnimationFile(scriptPath);
	logMemoryPlatform();


	char palettePath[1024];
	int preferredPalette = tPlayer->mPreferredPalette;
	sprintf(name, "pal%d", preferredPalette);
	getMugenDefStringOrDefault(file, tScript, "Files", name, "");
	int hasPalettePath = strcmp("", file);
	sprintf(palettePath, "%s%s", path, file);
	getMugenDefStringOrDefault(file, tScript, "Files", "sprite", "");
	assert(strcmp("", file));
	sprintf(scriptPath, "%s%s", path, file);

	tPlayer->mHeader->mFiles.mHasPalettePath = hasPalettePath;
	tPlayer->mHeader->mFiles.mPalettePath = copyToAllocatedString(palettePath);
	tPlayer->mHeader->mFiles.mSpritePath = copyToAllocatedString(scriptPath);

	getMugenDefStringOrDefault(file, tScript, "Files", "sound", "");
	sprintf(scriptPath, "%s%s", path, file);
	if (isFile(scriptPath) && !isOnDreamcast()) {
		setSoundEffectCompression(1);
		tPlayer->mHeader->mFiles.mSounds = loadMugenSoundFile(scriptPath);
		setSoundEffectCompression(0);
	}
	else {
		tPlayer->mHeader->mFiles.mSounds = createEmptyMugenSoundFile();
	}
	logMemoryPlatform();

	setPlayerExternalDependencies(tPlayer);

	if (getPlayerAILevel(tPlayer)) {
		setDreamAIActive(tPlayer);
	}

	if (doesDreamPlayerStartFacingLeft(tPlayer->mRootID)) {
		setPlayerFaceDirection(tPlayer, FACE_DIRECTION_LEFT);
	}
	else {
		setPlayerFaceDirection(tPlayer, FACE_DIRECTION_RIGHT);
	}
}

static void initHitDefAttributeSlot(DreamHitDefAttributeSlot* tSlot) {
	tSlot->mIsActive = 0;
	tSlot->mNow = 0;
}

static void resetHelperState(DreamPlayer* p) {
	p->mHelpers = new_list();
	p->mProjectiles = new_int_map();

	p->mReceivedHitData = new_list();

	p->mNoWalkFlag = 0;
	p->mNoAutoTurnFlag = 0;
	p->mNoLandFlag = 0;
	p->mPushDisabledFlag = 0;
	p->mNoAirGuardFlag = 0;
	p->mNoCrouchGuardFlag = 0;
	p->mNoStandGuardFlag = 0;
	p->mNoKOSoundFlag = 0;
	p->mNoKOSlowdownFlag = 0;
	p->mUnguardableFlag = 0;
	p->mTransparencyFlag = 0;
	p->mWidthFlag = 0;
	p->mNoJuggleCheckFlag = 0;
	p->mDrawOffset = makePosition(0, 0, 0);
	p->mJumpFlank = 0;
	p->mAirJumpCounter = 0;

	p->mIsHitOver = 1;
	p->mIsFalling = 0;
	p->mCanRecoverFromFall = 0;

	p->mIsHitShakeActive = 0;
	p->mHitShakeNow = 0;
	p->mHitShakeDuration = 0;

	p->mIsHitOverWaitActive = 0;
	p->mHitOverNow = 0;
	p->mHitOverDuration = 0;

	p->mIsAngleActive = 0;
	p->mAngle = 0;

	p->mRelativeScale = makePosition(1, 1, 1);
	p->mTempScale = makePosition(1, 1, 1);

	p->mCheeseWinFlag = 0;
	p->mSuicideWinFlag = 0;

	p->mDefenseMultiplier = 1;

	p->mIsFrozen = 0;

	p->mIsLyingDown = 0;
	p->mIsHitPaused = 0;
	p->mMoveContactCounter = 0;

	p->mHitCount = 0;
	p->mFallAmountInCombo = 0;

	p->mAttackMultiplier = 1;

	initPlayerHitData(p);

	p->mFaceDirection = FACE_DIRECTION_RIGHT;

	p->mHasMoveBeenReversed = 0;
	p->mMoveHit = 0;
	p->mMoveGuarded = 0;
	p->mLastHitGuarded = 0;

	p->mHasLastContactProjectile = 0;

	p->mRoundsExisted = 0;

	p->mIsBound = 0;
	p->mBoundHelpers = new_list();

	p->mTargetID = 0;
	p->mIsGuardingInternally = 0;

	p->mIsBeingJuggled = 0;
	p->mComboCounter = 0;
	p->mDisplayedComboCounter = 0;
	p->mIsDestroyed = 0;

	int i;
	for (i = 0; i < 2; i++) {
		initHitDefAttributeSlot(&p->mNotHitBy[i]);
		p->mDustClouds[i].mLastDustTime = 0;
	}
}

static void loadPlayerState(DreamPlayer* p) {
	memset(p->mVars, 0, sizeof p->mVars);
	memset(p->mSystemVars, 0, sizeof p->mSystemVars);
	memset(p->mFloatVars, 0, sizeof p->mFloatVars);
	memset(p->mSystemFloatVars, 0, sizeof p->mSystemFloatVars);
	
	p->mID = 0;

	p->mIsInControl = 1;
	p->mIsAlive = 1;
	p->mRoundsWon = 0;

	resetHelperState(p);
	p->mIsHelper = 0;
	p->mParent = NULL;
	p->mHelperIDInParent = -1;
	p->mHelperIDInRoot = -1;
	p->mHelperIDInStore = -1;

	p->mIsProjectile = 0;
	p->mProjectileID = -1;
	p->mProjectileDataID = -1;

	p->mPower = 0; 
	
	p->mIsBoundToScreenForever = 1;
	p->mIsBoundToScreenForTick = 1;
}

static void loadPlayerStateWithConstantsLoaded(DreamPlayer* p) {
	p->mHeader->mFiles.mConstants.mHeader.mLife = (int)(p->mHeader->mFiles.mConstants.mHeader.mLife * getLifeStartPercentage());
	p->mLife = (int)(p->mHeader->mFiles.mConstants.mHeader.mLife * p->mStartLifePercentage);
	setPlayerDrawOffsetX(p, 0, getPlayerCoordinateP(p));
	setPlayerDrawOffsetY(p, 0, getPlayerCoordinateP(p));
}

static void loadPlayerShadow(DreamPlayer* p) {
	Position pos = getDreamStageCoordinateSystemOffset(getPlayerCoordinateP(p));
	pos.z = SHADOW_Z;
	p->mShadow.mShadowPosition = *getHandledPhysicsPositionReference(p->mPhysicsElement);
	p->mShadow.mAnimationElement = addMugenAnimation(getMugenAnimation(&p->mHeader->mFiles.mAnimations, getMugenAnimationAnimationNumber(p->mAnimationElement)), gPlayerDefinition.mIsLoading ? NULL : &p->mHeader->mFiles.mSprites, pos);
	setMugenAnimationBasePosition(p->mShadow.mAnimationElement, &p->mShadow.mShadowPosition);
	setMugenAnimationCameraPositionReference(p->mShadow.mAnimationElement, getDreamMugenStageHandlerCameraPositionReference());
	setMugenAnimationDrawScale(p->mShadow.mAnimationElement, makePosition(1, -getDreamStageShadowScaleY(), 1) * p->mHeader->mFiles.mConstants.mSizeData.mScale);
	Vector3D color = getDreamStageShadowColor();
	(void)color; // TODO: proper shadow color (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/383)
	setMugenAnimationColor(p->mShadow.mAnimationElement, 0, 0, 0); // TODO: proper shadow color (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/383)
	setMugenAnimationTransparency(p->mShadow.mAnimationElement, getDreamStageShadowTransparency());
	setMugenAnimationFaceDirection(p->mShadow.mAnimationElement, getMugenAnimationIsFacingRight(p->mAnimationElement));
}

static void loadPlayerReflection(DreamPlayer* p) {
	Position pos = getDreamStageCoordinateSystemOffset(getPlayerCoordinateP(p));
	pos.z = REFLECTION_Z;
	p->mReflection.mPosition = *getHandledPhysicsPositionReference(p->mPhysicsElement);
	p->mReflection.mAnimationElement = addMugenAnimation(getMugenAnimation(&p->mHeader->mFiles.mAnimations, getMugenAnimationAnimationNumber(p->mAnimationElement)), gPlayerDefinition.mIsLoading ? NULL : &p->mHeader->mFiles.mSprites, pos);

	setMugenAnimationBasePosition(p->mReflection.mAnimationElement, &p->mReflection.mPosition);
	setMugenAnimationCameraPositionReference(p->mReflection.mAnimationElement, getDreamMugenStageHandlerCameraPositionReference());
	setMugenAnimationDrawScale(p->mReflection.mAnimationElement, makePosition(1, -1, 1) * p->mHeader->mFiles.mConstants.mSizeData.mScale);
	setMugenAnimationBlendType(p->mReflection.mAnimationElement, BLEND_TYPE_ADDITION);
	setMugenAnimationTransparency(p->mReflection.mAnimationElement, getDreamStageReflectionTransparency());
	setMugenAnimationFaceDirection(p->mReflection.mAnimationElement, getMugenAnimationIsFacingRight(p->mAnimationElement));
}

static void loadPlayerDebug(DreamPlayer* p) {
	setMugenAnimationCollisionDebug(p->mAnimationElement, gPlayerDefinition.mIsCollisionDebugActive);

	char text[2];
	text[0] = '\0';
	p->mDebug.mCollisionTextID = addMugenText(text, makePosition(0, 0, 1), -1);
	setMugenTextAlignment(p->mDebug.mCollisionTextID, MUGEN_TEXT_ALIGNMENT_CENTER);
}

static void loadSinglePlayerFromMugenDefinition(DreamPlayer* p)
{
	MugenDefScript script; 
	loadMugenDefScript(&script, p->mHeader->mFiles.mDefinitionPath);

	loadPlayerState(p);
	loadPlayerHeaderFromScript(p->mHeader, &script);
	loadPlayerFiles(p->mHeader->mFiles.mDefinitionPath, p, &script);
	loadPlayerStateWithConstantsLoaded(p);
	loadPlayerShadow(p);
	loadPlayerReflection(p);
	loadPlayerDebug(p);
	unloadMugenDefScript(script);
	

}

void loadPlayers(MemoryStack* tMemoryStack) {
	gPlayerDefinition.mIsLoading = 1;

	gPlayerDefinition.mTimeDilatationNow = 0.0;
	gPlayerDefinition.mTimeDilatation = 1.0;
	gPlayerDefinition.mTimeDilatationUpdates = 1;
	gPlayerDefinition.mHelperStore.clear();
	gPlayerDefinition.mAllPlayers = new_list();
	list_push_back(&gPlayerDefinition.mAllPlayers, &gPlayerDefinition.mPlayers[0]);
	list_push_back(&gPlayerDefinition.mAllPlayers, &gPlayerDefinition.mPlayers[1]);

	gPlayerDefinition.mMemoryStack = tMemoryStack;
	int i = 0;
	for (i = 0; i < 2; i++) {
		gPlayerDefinition.mPlayers[i].mHeader = &gPlayerDefinition.mPlayerHeader[i];
		gPlayerDefinition.mPlayers[i].mRoot = &gPlayerDefinition.mPlayers[i];
		gPlayerDefinition.mPlayers[i].mOtherPlayer = &gPlayerDefinition.mPlayers[i ^ 1];
		gPlayerDefinition.mPlayers[i].mRootID = i;
		gPlayerDefinition.mPlayers[i].mControllerID = i;
		loadSinglePlayerFromMugenDefinition(&gPlayerDefinition.mPlayers[i]);
	}

	gPlayerDefinition.mIsLoading = 0;
	gPlayerDefinition.mHasLoadedSprites = 0;
}

static void loadSinglePlayerSprites(DreamPlayer* tPlayer) {
	setMugenSpriteFileReaderToUsePalette(tPlayer->mRootID);
	tPlayer->mHeader->mFiles.mSprites = loadMugenSpriteFile(tPlayer->mHeader->mFiles.mSpritePath, tPlayer->mHeader->mFiles.mHasPalettePath, tPlayer->mHeader->mFiles.mPalettePath);
	setMugenSpriteFileReaderToNotUsePalette();
	logMemoryPlatform();

	setMugenAnimationSprites(tPlayer->mAnimationElement, &tPlayer->mHeader->mFiles.mSprites);
	setMugenAnimationSprites(tPlayer->mShadow.mAnimationElement, &tPlayer->mHeader->mFiles.mSprites);
	setMugenAnimationSprites(tPlayer->mReflection.mAnimationElement, &tPlayer->mHeader->mFiles.mSprites);
}

void loadPlayerSprites() {
	int i;
	for (i = 0; i < 2; i++) {
		loadSinglePlayerSprites(&gPlayerDefinition.mPlayers[i]);
	}

	gPlayerDefinition.mHasLoadedSprites = 1;
}

static void unloadHelperStateWithoutFreeingOwnedHelpersAndProjectile(DreamPlayer* p) {
	// projectiles shouldn't have helpers, so no need to move them
	delete_list(&p->mHelpers);
	delete_int_map(&p->mProjectiles);
	delete_list(&p->mReceivedHitData);

	delete_list(&p->mBoundHelpers);
}

static void unloadPlayerFiles(DreamPlayerHeader* tHeader) {
	//unloadDreamMugenConstantsFile(&tHeader->mFiles.mConstants);
	unloadDreamMugenCommandFile(&tHeader->mFiles.mCommands);
	//unloadMugenAnimationFile(&tHeader->mFiles.mAnimations);
	//unloadMugenSpriteFile(&tHeader->mFiles.mSprites);
	//unloadMugenSoundFile(&tHeader->mFiles.mSounds);

}

static void unloadSinglePlayer(DreamPlayer* /*p*/, DreamPlayerHeader* tHeader) {
	// TODO: unload player state (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/381)
	unloadPlayerFiles(tHeader);
}

static void unloadPlayerHeader(int i) {
	gPlayerDefinition.mPlayerHeader[i].mFiles.mConstants.mStates.mStates = std::map<int, DreamMugenState>();

	gPlayerDefinition.mPlayerHeader[i].mCustomOverrides.mHasCustomDisplayName = 0;
}

void unloadPlayers() {
	int i;
	for (i = 0; i < 2; i++) {
		unloadPlayerHeader(i);
		unloadSinglePlayer(&gPlayerDefinition.mPlayers[i], &gPlayerDefinition.mPlayerHeader[i]);
	}

	gPlayerDefinition.mHelperStore.clear();
	//delete_list(&gPlayerDefinition.mAllPlayers);
}

static void removeSingleHelperCB(void* /*tCaller*/, void* tData) {
	DreamPlayer* p = (DreamPlayer*)tData;
	list_map(&p->mHelpers, removeSingleHelperCB, NULL);
	destroyPlayer(p);
}

static void removePlayerHelpers(DreamPlayer* p) {
	list_map(&p->mHelpers, removeSingleHelperCB, NULL);
}

static void forceUnpausePlayer(DreamPlayer* p);

static void resetSinglePlayer(DreamPlayer* p) {
	p->mIsAlive = 1;

	p->mLife = (int)(p->mHeader->mFiles.mConstants.mHeader.mLife * p->mStartLifePercentage);
	setDreamLifeBarPercentage(p, p->mStartLifePercentage);

	forceUnpausePlayer(p);
	resetPlayerPosition(p);

	if (doesDreamPlayerStartFacingLeft(p->mRootID)) {
		setPlayerFaceDirection(p, FACE_DIRECTION_LEFT);
	}
	else {
		setPlayerFaceDirection(p, FACE_DIRECTION_RIGHT);
	}

	removePlayerHelpers(p);
}

void resetPlayers()
{
	resetSinglePlayer(&gPlayerDefinition.mPlayers[0]);
	resetSinglePlayer(&gPlayerDefinition.mPlayers[1]);
	removeAllExplods();
}

static void resetSinglePlayerEntirely(DreamPlayer* p) {
	p->mRoundsWon = 0;
	p->mRoundsExisted = 0;
	p->mPower = 0;
	setDreamPowerBarPercentage(p, 0, p->mPower);
}

void resetPlayersEntirely()
{
	resetPlayers();
	resetSinglePlayerEntirely(&gPlayerDefinition.mPlayers[0]);
	resetSinglePlayerEntirely(&gPlayerDefinition.mPlayers[1]);
}

void resetPlayerPosition(DreamPlayer * p)
{
	setPlayerPosition(p, getDreamPlayerStartingPosition(p->mRootID, getPlayerCoordinateP(p)), getPlayerCoordinateP(p));
}

static int isPlayerGuarding(DreamPlayer* p);

static void updateWalking(DreamPlayer* p) {
	if (p->mIsHelper) return;
	if (p->mNoWalkFlag) {
		p->mNoWalkFlag = 0;
		return;
	}

	if (!p->mIsInControl) return;
	if (isPlayerGuarding(p)) return;

	if (getPlayerStateType(p) != MUGEN_STATE_TYPE_STANDING) return;

	if (isPlayerCommandActive(p, "holdfwd") || isPlayerCommandActive(p, "holdback")) {
		if (getPlayerState(p) != 20) {
			changePlayerState(p, 20);
		}
	}
	else if (getPlayerState(p) == 20) {
		changePlayerState(p, 0);
	}
}

static void updateAirJumping(DreamPlayer* p) {
	if (p->mIsHelper) return;
	if (!p->mIsInControl) return;
	if (getPlayerStateType(p) != MUGEN_STATE_TYPE_AIR) return;

	int hasJumpLeft = p->mAirJumpCounter < p->mHeader->mFiles.mConstants.mMovementData.mAllowedAirJumpAmount;
	int hasMinimumHeight = -getPlayerPositionY(p, getPlayerCoordinateP(p)) >= p->mHeader->mFiles.mConstants.mMovementData.mAirJumpMinimumHeight;

	if (isPlayerCommandActive(p, "holdup") && p->mJumpFlank && hasJumpLeft && hasMinimumHeight) {
		p->mJumpFlank = 0;
		p->mAirJumpCounter++;
		changePlayerState(p, 45);
	}
}

static void updateJumpFlank(DreamPlayer* p) {
	if (p->mJumpFlank) return;
	p->mJumpFlank |= !isPlayerCommandActive(p, "holdup");
}

static void updateJumping(DreamPlayer* p) {
	if (p->mIsHelper) return;
	if (!p->mIsInControl) return;
	if (getPlayerStateType(p) != MUGEN_STATE_TYPE_STANDING) return;


	if (isPlayerCommandActive(p, "holdup")) {
		changePlayerState(p, 40);
	}
}

static void updateLanding(DreamPlayer* p) {
	if (p->mIsHelper) return;
	if (p->mNoLandFlag) {
		p->mNoLandFlag = 0;
		return;
	}

	Position pos = *getHandledPhysicsPositionReference(p->mPhysicsElement);
	if (getPlayerPhysics(p) != MUGEN_STATE_PHYSICS_AIR) return;
	if (isPlayerPaused(p)) return;


	Velocity vel = *getHandledPhysicsVelocityReference(p->mPhysicsElement);
	if (pos.y >= 0 && vel.y >= 0) {
		setPlayerControl(p, 1);
		changePlayerState(p, 52);
	}
}

static void updateCrouchingDown(DreamPlayer* p) {
	if (p->mIsHelper) return;
	if (!p->mIsInControl) return;
	if (getPlayerStateType(p) != MUGEN_STATE_TYPE_STANDING) return;


	if (isPlayerCommandActive(p, "holddown")) {
		changePlayerState(p, 10);
	}
}

static void updateStandingUp(DreamPlayer* p) {
	if (p->mIsHelper) return;
	if (!p->mIsInControl) return;
	if (getPlayerStateType(p) != MUGEN_STATE_TYPE_CROUCHING) return;


	if (!isPlayerCommandActive(p, "holddown")) {
		changePlayerState(p, 12);
	}
}

static void setPlayerFaceDirection(DreamPlayer* p, FaceDirection tDirection) {
	if (p->mFaceDirection == tDirection) return;

	setMugenAnimationFaceDirection(p->mAnimationElement, tDirection == FACE_DIRECTION_RIGHT);

	if (!p->mIsHelper) {
		setDreamMugenCommandFaceDirection(p->mCommandID, tDirection);
	}

	p->mFaceDirection = tDirection;
}

static void updateAutoTurn(DreamPlayer* p) {
	if (p->mNoAutoTurnFlag) {
		p->mNoAutoTurnFlag = 0;
		return;
	}

	if (!p->mIsInControl) return;

	if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) return;

	turnPlayerTowardsOtherPlayer(p);
}

static void updatePositionFreeze(DreamPlayer* p) {
	if (p->mIsFrozen) {
		Position* pos = getHandledPhysicsPositionReference(p->mPhysicsElement);
		*pos = p->mFreezePosition;
		p->mIsFrozen = 0;
	}
}

static void updateGettingUp(DreamPlayer* p) {
	if (p->mIsHelper) return;
	if (getPlayerState(p) != 5110 && !p->mIsLyingDown) return;

	if (!p->mIsLyingDown) {
		increasePlayerFallAmountInCombo(p);
		p->mLyingDownTime = 0;
		p->mIsLyingDown = 1;
		return;
	}

	if (p->mIsLyingDown && getPlayerState(p) != 5110) {
		p->mIsLyingDown = 0;
		return;
	}

	p->mLyingDownTime++;
	if (hasPressedAnyButtonFlankSingle(p->mRootID)) p->mLyingDownTime++;
	if (p->mLyingDownTime >= p->mHeader->mFiles.mConstants.mHeader.mLiedownTime) {
		p->mIsLyingDown = 0;
		changePlayerState(p, 5120);
	}

}

static void updateFall(DreamPlayer* p) {
	if (p->mIsHelper) return;
	if (!p->mIsFalling) return;

	if (!p->mIsHitPaused) {
		p->mRecoverTimeSinceHitPause++;
	}
}

static void updateHitPause(DreamPlayer* p) {
	if (!p->mIsHitPaused) {
		if (p->mMoveHit) p->mMoveHit++;
		if (p->mMoveGuarded) p->mMoveGuarded++;
		if (p->mHasMoveBeenReversed) p->mHasMoveBeenReversed++; // TODO: reversaldef (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/632)
		if (p->mMoveContactCounter) p->mMoveContactCounter++;
		return;
	}
	
	p->mHitPauseNow++;
	if (p->mHitPauseNow >= p->mHitPauseDuration) {
		setPlayerUnHitPaused(p);
	}
}

static void updateBindingPosition(DreamPlayer* p) {
	DreamPlayer* bindRoot = p->mBoundTarget;
	Position pos = getPlayerPosition(bindRoot, getPlayerCoordinateP(p));

	if (p->mBoundPositionType == PLAYER_BIND_POSITION_TYPE_HEAD) {
		pos = vecAdd(pos, getPlayerHeadPosition(bindRoot));
	}
	else if (p->mBoundPositionType == PLAYER_BIND_POSITION_TYPE_MID) {
		pos = vecAdd(pos, getPlayerMiddlePosition(bindRoot));
	}

	if (getPlayerIsFacingRight(bindRoot)) {
		pos = vecAdd(pos, p->mBoundOffset);
	}
	else {
		pos = vecAdd(pos, makePosition(-p->mBoundOffset.x, p->mBoundOffset.y, 0));
	}

	setPlayerPosition(p, pos, getPlayerCoordinateP(p));

	if (p->mBoundFaceSet) {
		if (p->mBoundFaceSet == 1) setPlayerIsFacingRight(p, getPlayerIsFacingRight(bindRoot));
		else setPlayerIsFacingRight(p, !getPlayerIsFacingRight(bindRoot));
	}
}

static void removePlayerBindingInternal(DreamPlayer* tPlayer) {
	if (!tPlayer->mIsBound) return;
	tPlayer->mIsBound = 0;

	DreamPlayer* boundTo = tPlayer->mBoundTarget;
	if (!isPlayer(boundTo)) return;
	list_remove(&boundTo->mBoundHelpers, tPlayer->mBoundID);
}

static void updateBinding(DreamPlayer* p) {
	if (!p->mIsBound) return;
	if (isPlayerPaused(p)) return;

	p->mBoundNow++;
	if (!isPlayer(p->mBoundTarget) || p->mBoundNow >= p->mBoundDuration) {
		removePlayerBindingInternal(p);
		return;
	}

	updateBindingPosition(p);
}

static int isPlayerGuarding(DreamPlayer* p) {
	(void)p;
	return getPlayerState(p) >= 120 && getPlayerState(p) <= 155;
}


static int doesFlagPreventGuarding(DreamPlayer* p) {
	if (getPlayerStateType(p) == MUGEN_STATE_TYPE_STANDING && p->mNoStandGuardFlag) return 1;
	if (getPlayerStateType(p) == MUGEN_STATE_TYPE_CROUCHING && p->mNoCrouchGuardFlag) return 1;
	if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR && p->mNoAirGuardFlag) return 1;

	return 0;
}

static void setPlayerGuarding(DreamPlayer* p) {
	p->mIsGuardingInternally = 1;
	changePlayerState(p, 120);
}

static void setPlayerUnguarding(DreamPlayer* p) {
	p->mIsGuardingInternally = 0;

	if (getPlayerStateType(p) == MUGEN_STATE_TYPE_STANDING) {
		changePlayerState(p, 0);
	}
	else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_CROUCHING) {
		changePlayerState(p, 11);
	}
	else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) {
		changePlayerState(p, 51);
	}
	else {
		logWarningFormat("Unknown player state %d. Defaulting to state 0.", getPlayerStateType(p));
		changePlayerState(p, 0);
	}
}

static void updateGuarding(DreamPlayer* p) {
	if (p->mIsHelper) return;
	if (isPlayerPaused(p)) return;
	if (!p->mIsInControl) return;
	if (isPlayerGuarding(p)) {
		return;
	}
	if (doesFlagPreventGuarding(p)) return;

	if (isPlayerCommandActive(p, "holdback") && isPlayerBeingAttacked(p) && isPlayerInGuardDistance(p)) {
		setPlayerGuarding(p);
	}
}

static void updateGuardingOver(DreamPlayer* p) {
	if (p->mIsHelper) return;
	if (isPlayerPaused(p)) return;
	if (!p->mIsInControl) return;
	if (getPlayerState(p) != 140) return;
	if (getPlayerAnimationTimeDeltaUntilFinished(p)) return;

	setPlayerUnguarding(p);
}

static int isPlayerGuardingInternally(DreamPlayer* p) {
	return p->mIsGuardingInternally;
}

static void updateGuardingFlags(DreamPlayer* p) {
	if (isPlayerGuardingInternally(p) && doesFlagPreventGuarding(p)) {
		setPlayerUnguarding(p);
	}

	p->mNoAirGuardFlag = 0;
	p->mNoCrouchGuardFlag = 0;
	p->mNoStandGuardFlag = 0;
	p->mUnguardableFlag = 0;
}

static int updateSinglePlayer(DreamPlayer* p);

static int updateSinglePlayerCB(void* tCaller, void* tData) {
	(void)tCaller;
	DreamPlayer* p = (DreamPlayer*)tData;
	return updateSinglePlayer(p);
}

static void updateSingleHitAttributeSlot(DreamHitDefAttributeSlot* tSlot) {
	if (!tSlot->mIsActive) return;

	tSlot->mNow++;
	if (tSlot->mNow >= tSlot->mTime) {
		tSlot->mIsActive = 0;
	}
}

static void updateHitAttributeSlots(DreamPlayer* p) {
	if (isPlayerPaused(p)) return;

	int i;
	for (i = 0; i < 2; i++) {
		updateSingleHitAttributeSlot(&p->mNotHitBy[i]);
	}
}

static void updateProjectileTimeSinceContact(DreamPlayer* p) {
	if (!p->mHasLastContactProjectile) return;

	p->mLastContactProjectileTime++;
}

static void updatePlayerAirJuggle(DreamPlayer* p) {
	if (!p->mIsBeingJuggled) return;
	if (!getPlayerControl(p)) return;

	p->mIsBeingJuggled = 0;
	p->mAirJugglePoints = p->mHeader->mFiles.mConstants.mHeader.mAirJugglePoints;
}

static void updatePush(DreamPlayer* p) {
	if (isPlayerPaused(p)) return;
	if (isPlayerHelper(p)) return;
	if (p->mPushDisabledFlag) return;
	if (p->mIsBound) return;
	if (list_size(&p->mBoundHelpers)) return;

	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p);
	if (otherPlayer->mPushDisabledFlag) return;

	double frontX1 = getPlayerFrontXPlayer(p, getPlayerCoordinateP(p));
	double frontX2 = getPlayerFrontXPlayer(otherPlayer, getPlayerCoordinateP(p));

	double x1 = getPlayerPositionX(p, getPlayerCoordinateP(p));
	double x2 = getPlayerPositionX(otherPlayer, getPlayerCoordinateP(p));
	
	double distX = getPlayerDistanceToFrontOfOtherPlayerX(p);
	double distY = getPlayerAxisDistanceY(p);

	if (distY >= 0 && distY >= p->mHeader->mFiles.mConstants.mSizeData.mHeight) return;
	if (distY <= 0 && distY <= -p->mHeader->mFiles.mConstants.mSizeData.mHeight) return;

	if (x1 < x2) {
		if (frontX1 > frontX2) {
			setPlayerPositionX(p, x1 - distX / 2, getPlayerCoordinateP(p));
			setPlayerPositionX(otherPlayer, x2 + distX / 2, getPlayerCoordinateP(p));
		}
	}
	else {
		if (frontX1 < frontX2) {
			setPlayerPositionX(p, x1 + distX / 2, getPlayerCoordinateP(p));
			setPlayerPositionX(otherPlayer, x2 - distX / 2, getPlayerCoordinateP(p));
		}
	}

}

static void updateAngle(DreamPlayer* p) {
	if (p->mIsAngleActive) {
		setMugenAnimationDrawAngle(p->mAnimationElement, p->mAngle);
		p->mIsAngleActive = 0;
	}
	else {
		setMugenAnimationDrawAngle(p->mAnimationElement, 0);
	}
}

static void updateScale(DreamPlayer* p) {
	setMugenAnimationDrawScale(p->mAnimationElement, p->mHeader->mFiles.mConstants.mSizeData.mScale * p->mRelativeScale * p->mTempScale);
	p->mTempScale = makePosition(1, 1, 1);
}

static void updatePushFlags() {
	getRootPlayer(0)->mPushDisabledFlag = 0;
	getRootPlayer(1)->mPushDisabledFlag = 0;
}

#define CORNER_CHECK_EPSILON 1

static int isPlayerInCorner(DreamPlayer* p, int tIsCheckingRightCorner) {
	double back = getPlayerBackXStage(p, getPlayerCoordinateP(p));
	double front = getPlayerFrontXStage(p, getPlayerCoordinateP(p));

	if (tIsCheckingRightCorner) {
		double right = getDreamStageRightOfScreenBasedOnPlayer(getPlayerCoordinateP(p));
		double maxX = max(back, front);
		return (maxX > right - CORNER_CHECK_EPSILON);
	}
	else {
		double left = getDreamStageLeftOfScreenBasedOnPlayer(getPlayerCoordinateP(p));
		double minX = min(back, front);
		return (minX < left + CORNER_CHECK_EPSILON);
	}
}

static void updateStageBorderPost(DreamPlayer* p) {
	if (!p->mIsBoundToScreenForTick || !p->mIsBoundToScreenForever) return;

	double left = getDreamStageLeftOfScreenBasedOnPlayer(getPlayerCoordinateP(p));
	double right = getDreamStageRightOfScreenBasedOnPlayer(getPlayerCoordinateP(p));
	// int lx = getDreamStageLeftEdgeMinimumPlayerDistance(getPlayerCoordinateP(p));
	// int rx = getDreamStageRightEdgeMinimumPlayerDistance(getPlayerCoordinateP(p));

	double back = getPlayerBackXStage(p, getPlayerCoordinateP(p));
	double front = getPlayerFrontXStage(p, getPlayerCoordinateP(p));
	double minX = min(back, front);
	double maxX = max(back, front);

	double x = getPlayerPositionX(p, getPlayerCoordinateP(p));

	if (minX < left) {
		x += left - minX;
		setPlayerPositionX(p, x, getPlayerCoordinateP(p));
	}
	else if (maxX > right) {
		x -= maxX - right;
		setPlayerPositionX(p, x, getPlayerCoordinateP(p));
	}
}

static void updatePlayerReceivedHits(DreamPlayer* p);

static void updateStageBorderPre(DreamPlayer* p) {
	if (!p->mIsBoundToScreenForTick || !p->mIsBoundToScreenForever) {
		p->mIsBoundToScreenForTick = 1;
		return;
	}

	updateStageBorderPost(p);
}

static void updateKOFlags(DreamPlayer* p) {
	p->mNoKOSoundFlag = 0;
	p->mNoKOSlowdownFlag = 0;
}

static void updateTransparencyFlag(DreamPlayer* p) {
	if (!p->mTransparencyFlag) return;

	setMugenAnimationBlendType(p->mAnimationElement, BLEND_TYPE_NORMAL);
	setMugenAnimationTransparency(p->mAnimationElement, 1);

	p->mTransparencyFlag = 0;
}

static void updateShadow(DreamPlayer* p) {

	setMugenAnimationFaceDirection(p->mShadow.mAnimationElement, getMugenAnimationIsFacingRight(p->mAnimationElement));

	p->mShadow.mShadowPosition = *getHandledPhysicsPositionReference(p->mPhysicsElement);
	if (!p->mInvisibilityFlag) {
		setMugenAnimationVisibility(p->mShadow.mAnimationElement, p->mShadow.mShadowPosition.y <= 0);
	}

	p->mShadow.mShadowPosition.y *= -getDreamStageShadowScaleY();
	
	Vector3D fadeRange = getDreamStageShadowFadeRange(getPlayerCoordinateP(p));
	fadeRange = vecScale(fadeRange, 0.5);
	double posY = -(getDreamScreenHeight(getPlayerCoordinateP(p))-getPlayerScreenPositionY(p, getPlayerCoordinateP(p)));

	if (posY <= fadeRange.x) {
		setMugenAnimationTransparency(p->mShadow.mAnimationElement, 0);
	}
	else if (posY <= fadeRange.y) {
		double t = 1-(((-posY) - (-fadeRange.y)) / ((-fadeRange.x) - (-fadeRange.y)));
		setMugenAnimationTransparency(p->mShadow.mAnimationElement, t*getDreamStageShadowTransparency());
	}
	else {
		setMugenAnimationTransparency(p->mShadow.mAnimationElement, getDreamStageShadowTransparency());
	}
}

static void updateReflection(DreamPlayer* p) {
	setMugenAnimationFaceDirection(p->mReflection.mAnimationElement, getMugenAnimationIsFacingRight(p->mAnimationElement));

	p->mReflection.mPosition = *getHandledPhysicsPositionReference(p->mPhysicsElement);
	if (!p->mInvisibilityFlag) {
		setMugenAnimationVisibility(p->mReflection.mAnimationElement, p->mReflection.mPosition.y <= 0);
	}
	p->mReflection.mPosition.y *= -1;

}

static void updatePlayerPhysicsClamp(DreamPlayer* p) {
	auto vel = getHandledPhysicsVelocityReference(p->mPhysicsElement);
	if (!vel->x && !vel->y) return;
	const double PHYSICS_EPSILON = 1e-6;
	if (fabs(vel->x) < PHYSICS_EPSILON) vel->x = 0;
	if (fabs(vel->y) < PHYSICS_EPSILON) vel->y = 0;
}

static void updateSingleProjectile(DreamPlayer* p) {
	updateShadow(p);
	updateReflection(p);
	updatePlayerPhysicsClamp(p);
}

static void updateSingleProjectileCB(void* tCaller, void* tData) {
	(void)tCaller;
	updateSingleProjectile((DreamPlayer*)tData);
}

static void updatePlayerTrainingMode(DreamPlayer* p) {
	if (!gPlayerDefinition.mIsInTrainingMode) return;
	if (!getPlayerControl(p)) return;

	addPlayerLife(p, p, 3);
	addPlayerPower(p, 50);
}

static void updatePlayerDebug(DreamPlayer* p) {
	if (!gPlayerDefinition.mIsCollisionDebugActive) return;

	double sx = getPlayerScreenPositionX(p, getPlayerCoordinateP(p));
	double sy = getPlayerScreenPositionY(p, getPlayerCoordinateP(p));

	Position pos = makePosition(sx, sy+5, PLAYER_DEBUG_TEXT_Z);
	setMugenTextPosition(p->mDebug.mCollisionTextID, pos);

	char text[100];
	if (getPlayerStateMoveType(p) == MUGEN_STATE_MOVE_TYPE_ATTACK) {
		sprintf(text, "%s %d H", getPlayerDisplayName(p), getPlayerID(p));
	}
	else {
		sprintf(text, "%s %d", getPlayerDisplayName(p), getPlayerID(p));
	}

	changeMugenText(p->mDebug.mCollisionTextID, text);
}

static void updatePlayerDisplayedCombo(DreamPlayer* p) {
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p);

	if (getPlayerControl(otherPlayer)) {
		p->mDisplayedComboCounter = 0;
	}
}

static void updateBeingTarget(DreamPlayer* p) {
	if (getPlayerControl(p) && p->mTargetID) {
		p->mTargetID = 0;
	}
}

static void setPlayerHitOver(DreamPlayer* p) {
	setActiveHitDataInactive(p);
	p->mIsHitOverWaitActive = 0;
	p->mIsHitOver = 1;
}

static void updatePlayerHitOver(DreamPlayer* p) {
	if (!p->mIsHitOverWaitActive) return;

	p->mHitOverNow++;
	if (p->mHitOverNow >= p->mHitOverDuration) {
		setPlayerHitOver(p);
	}
}

static void setPlayerHitShakeOver(DreamPlayer* p) {
	p->mIsHitShakeActive = 0;

	int hitDuration;
	if (isPlayerGuarding(p)) {
		hitDuration = getActiveHitDataGuardHitTime(p);
	}
	else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) {
		hitDuration = getActiveHitDataAirHitTime(p);
	}
	else {
		hitDuration = getActiveHitDataGroundHitTime(p);
	}

	setPlayerVelocityX(p, getActiveHitDataVelocityX(p), getPlayerCoordinateP(p));
	setPlayerVelocityY(p, getActiveHitDataVelocityY(p), getPlayerCoordinateP(p));

	p->mIsHitOverWaitActive = 1;
	p->mHitOverNow = 0;
	p->mHitOverDuration = hitDuration;
}

static void updatePlayerHitShake(DreamPlayer* p) {
	if (!p->mIsHitShakeActive) return;

	p->mHitShakeNow++;
	if (p->mHitShakeNow > p->mHitShakeDuration) {
		setPlayerHitShakeOver(p);
	}
}

static void updatePlayerDestruction(DreamPlayer* p) {
	if (gPlayerDefinition.mHelperStore.find(p->mHelperIDInStore) == gPlayerDefinition.mHelperStore.end()) {
		logErrorFormat("Unable to delete helper %d %d, unable to find id %d in store. Ignoring.", p->mRootID, p->mID, p->mHelperIDInStore);
		return;
	}
	gPlayerDefinition.mHelperStore.erase(p->mHelperIDInStore);
}

static int updateSinglePlayer(DreamPlayer* p) {
	if (p->mIsDestroyed) {
		updatePlayerDestruction(p);

		return 1;
	}

	updateLanding(p);
	updateHitAttributeSlots(p);
	updateProjectileTimeSinceContact(p);
	updateGuarding(p);
	updateGuardingOver(p);
	updateGuardingFlags(p);
	updatePlayerAirJuggle(p);
	updatePlayerDisplayedCombo(p);
	updatePush(p);
	updateAngle(p);
	updateScale(p);
	updateStageBorderPost(p);
	updateKOFlags(p);
	updateTransparencyFlag(p);
	updateShadow(p);
	updateReflection(p);
	updatePlayerPhysicsClamp(p);
	updateBeingTarget(p);
	updatePlayerTrainingMode(p);
	updatePlayerDebug(p);

	list_remove_predicate(&p->mHelpers, updateSinglePlayerCB, NULL);
	int_map_map(&p->mProjectiles, updateSingleProjectileCB, NULL);
	return 0;
}

void updatePlayers()
{
	for (int currentUpdate = 0; currentUpdate < gPlayerDefinition.mTimeDilatationUpdates; currentUpdate++) {
		int i;
		for (i = 0; i < 2; i++) {
			updateSinglePlayer(&gPlayerDefinition.mPlayers[i]);
		}

		updatePushFlags();
	}
}

static void updatePlayersWithCaller(void* /*tCaller*/) {
	updatePlayers();
}

static int updateSinglePlayerPreStateMachine(DreamPlayer* p);
static int updateSinglePlayerPreStateMachineCB(void* tCaller, void* tData) {
	(void)tCaller;
	DreamPlayer* p = (DreamPlayer*)tData;
	return updateSinglePlayerPreStateMachine(p);
}

static void updateWidthFlag(DreamPlayer* tPlayer) {
	tPlayer->mWidthFlag = 0;
}

static void updateInvisibilityFlag(DreamPlayer* tPlayer) {
	tPlayer->mInvisibilityFlag = 0;
}

static void updateNoJuggleCheckFlag(DreamPlayer* tPlayer) {
	tPlayer->mNoJuggleCheckFlag = 0;
}

static void updateOffsetFlag(DreamPlayer* tPlayer) {
	if (tPlayer->mDrawOffset.x) {
		setPlayerDrawOffsetX(tPlayer, 0, getPlayerCoordinateP(tPlayer));
	}

	if (tPlayer->mDrawOffset.y) {
		setPlayerDrawOffsetY(tPlayer, 0, getPlayerCoordinateP(tPlayer));
	}
}

static int updateSinglePlayerPreStateMachine(DreamPlayer* p) {
	if (p->mIsDestroyed) {
		updatePlayerDestruction(p);
		return 1;
	}
	updatePlayerHitData(p);
	updatePlayerHitOver(p);
	updatePlayerHitShake(p);
	updateHitPause(p);
	updatePlayerReceivedHits(p);
	updateStageBorderPre(p);
	updatePush(p);
	updateWidthFlag(p);
	updateInvisibilityFlag(p);
	updateNoJuggleCheckFlag(p);
	updateOffsetFlag(p);

	updateWalking(p);
	updateAutoTurn(p);
	updateAirJumping(p);
	updateJumpFlank(p);
	updateJumping(p);
	updateCrouchingDown(p);
	updateStandingUp(p);
	updatePositionFreeze(p);
	updateGettingUp(p);
	updateFall(p);
	updateBinding(p);

	list_remove_predicate(&p->mHelpers, updateSinglePlayerPreStateMachineCB, NULL);
	return 0;
}


static void updatePlayersPreStateMachine(void* tData) {
	(void)tData;
	gPlayerDefinition.mTimeDilatationNow += gPlayerDefinition.mTimeDilatation;
	gPlayerDefinition.mTimeDilatationUpdates = (int)gPlayerDefinition.mTimeDilatationNow;
	gPlayerDefinition.mTimeDilatationNow -= gPlayerDefinition.mTimeDilatationUpdates;
	for (int currentUpdate = 0; currentUpdate < gPlayerDefinition.mTimeDilatationUpdates; currentUpdate++) {
		int i;
		for (i = 0; i < 2; i++) {
			updateSinglePlayerPreStateMachine(&gPlayerDefinition.mPlayers[i]);
		}
	}
}

static void drawSinglePlayer(DreamPlayer* p);

static void drawSinglePlayerCB(void* tCaller, void* tData) {
	(void)tCaller;
	DreamPlayer* p = (DreamPlayer*)tData;
	drawSinglePlayer(p);
}

static void drawPlayerWidthAndCenter(DreamPlayer* p) {
	double sx = getPlayerScreenPositionX(p, getPlayerCoordinateP(p));
	double sy = getPlayerScreenPositionY(p, getPlayerCoordinateP(p));
	
	double x = getPlayerPositionX(p, getPlayerCoordinateP(p));
	double fx = getPlayerFrontX(p, getPlayerCoordinateP(p));
	double bx = getPlayerBackX(p, getPlayerCoordinateP(p));

	double leftX, rightX;
	if (getPlayerIsFacingRight(p)) {
		rightX = sx + (fx - x);
		leftX = sx - (x - bx);
	}
	else {
		rightX = sx + (bx - x);
		leftX = sx - (x - fx);
	}

	Position pa = makePosition(leftX, sy, WIDTH_LINE_Z);
	Position pb = makePosition(rightX, sy, WIDTH_LINE_Z);
	drawColoredHorizontalLine(pa, pb, COLOR_DARK_GREEN);
	drawColoredPoint(makePosition(sx, sy, CENTER_POINT_Z), COLOR_YELLOW);
}

static void drawSinglePlayer(DreamPlayer* p) {
	if (p->mIsDestroyed) return;

	drawPlayerWidthAndCenter(p);


	list_map(&p->mHelpers, drawSinglePlayerCB, NULL);
	//int_map_map(&p->mProjectiles, drawSinglePlayerCB, NULL);
}

void drawPlayers() {
	if (!gPlayerDefinition.mIsCollisionDebugActive) return;

	int i;
	for (i = 0; i < 2; i++) {
		drawSinglePlayer(&gPlayerDefinition.mPlayers[i]);
	}
}

ActorBlueprint getPreStateMachinePlayersBlueprint() {
	return makeActorBlueprint(NULL, NULL, updatePlayersPreStateMachine);
}

ActorBlueprint getPostStateMachinePlayersBlueprint()
{
	return makeActorBlueprint(NULL, NULL, updatePlayersWithCaller);
}

int hasLoadedPlayerSprites()
{
	return gPlayerDefinition.mHasLoadedSprites;
}

static void handlePlayerHitOverride(DreamPlayer* p, DreamPlayer* tOtherPlayer, int* tNextState) {
	int doesForceAir;
	getMatchingHitOverrideStateNoAndForceAir(p, tOtherPlayer, tNextState, &doesForceAir);
	if (doesForceAir) {
		setPlayerUnguarding(p);
		setPlayerStateType(p, MUGEN_STATE_TYPE_AIR);
		setActiveHitDataAirFall(p, 1);
		p->mIsFalling = getActiveHitDataFall(p);
	}
}

static void setPlayerHitStatesPlayer(DreamPlayer* p, DreamPlayer* tOtherPlayer) {
	if (getActiveHitDataPlayer2StateNumber(p) == -1) {

		int nextState;

		if (hasMatchingHitOverride(p, tOtherPlayer)) {
			handlePlayerHitOverride(p, tOtherPlayer, &nextState);
		}
		else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_STANDING) {
			if (isPlayerGuarding(p)) {
				nextState = 150;
			}
			else if (getActiveHitDataGroundType(p) == MUGEN_ATTACK_HEIGHT_TRIP) {
				nextState = 5070;
			}
			else {
				nextState = 5000;
			}
		}
		else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_CROUCHING) {
			if (isPlayerGuarding(p)) {
				nextState = 152;
			}
			else if (getActiveHitDataGroundType(p) == MUGEN_ATTACK_HEIGHT_TRIP) {
				nextState = 5070;
			}
			else {
				nextState = 5010;
			}
		}
		else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) {
			if (isPlayerGuarding(p)) {
				nextState = 154;
			}
			else {
				nextState = 5020;
			}
		}
		else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_LYING) {
			if (getActiveHitDataGroundType(p) == MUGEN_ATTACK_HEIGHT_TRIP) {
				nextState = 5070;
			}
			else {
				nextState = 5080;
			}
		}
		else {
			logWarningFormat("Unrecognized player state type %d. Defaulting to state 5000.", getPlayerStateType(p));
			nextState = 5000;
		}

		changePlayerStateToSelf(p, nextState);
	}
	else if (getActiveHitDataPlayer2CapableOfGettingPlayer1State(p)) {
		changePlayerStateToOtherPlayerStateMachine(p, tOtherPlayer, getActiveHitDataPlayer2StateNumber(p));
	}
	else {
		changePlayerState(p, getActiveHitDataPlayer2StateNumber(p));
	}
}

static void setPlayerHitStatesNonPlayer(DreamPlayer* /*p*/, DreamPlayer* /*tOtherPlayer*/) {

}

static void setPlayerHitStates(DreamPlayer* p, DreamPlayer* tOtherPlayer) {
	if (isPlayerHelper(p) || isPlayerProjectile(p)) {
		setPlayerHitStatesNonPlayer(p, tOtherPlayer);
	}
	else {
		setPlayerHitStatesPlayer(p, tOtherPlayer);
	}

	int enemyStateBefore = getPlayerState(p);
	if (getActiveHitDataPlayer1StateNumber(p) != -1) {
		changePlayerState(tOtherPlayer, getActiveHitDataPlayer1StateNumber(p));
	}
	int enemyStateAfter = getPlayerState(p);

	if (enemyStateBefore == enemyStateAfter) {
		if (isPlayerHelper(p) || isPlayerProjectile(p)) {
			setPlayerHitStatesNonPlayer(p, tOtherPlayer);
		}
		else {
			setPlayerHitStatesPlayer(p, tOtherPlayer);
		}
	}
}

static int checkPlayerHitGuardFlagsAndReturnIfGuardable(DreamPlayer* tPlayer, char* tFlags) {
	DreamMugenStateType type = getPlayerStateType(tPlayer);
	char test[100];
	strcpy(test, tFlags);
	turnStringLowercase(test);

	if (type == MUGEN_STATE_TYPE_STANDING) {

		return strchr(test, 'h') != NULL || strchr(test, 'm') != NULL;
	} else  if (type == MUGEN_STATE_TYPE_CROUCHING) {
		return strchr(test, 'l') != NULL || strchr(test, 'm') != NULL;
	}
	else  if (type == MUGEN_STATE_TYPE_AIR) {
		return strchr(test, 'a') != NULL;
	}
	else {
		logWarningFormat("Unrecognized player type %d. Defaulting to unguardable.", type);
		return 0;
	}

}

static void setPlayerUnguarding(DreamPlayer* p);

static int isPlayerInCorner(DreamPlayer* p, int tIsCheckingRightCorner);

static void handlePlayerCornerPush(DreamPlayer* p, DreamPlayer* tOtherPlayer) {
	if (isPlayerHelper(tOtherPlayer) || isPlayerProjectile(tOtherPlayer)) return;
	if (getPlayerStateType(tOtherPlayer) == MUGEN_STATE_TYPE_AIR) return;
	if (!isPlayerInCorner(p, getActiveHitDataIsFacingRight(p))) return;
	if (getActiveHitDataAttackClass(p) == MUGEN_ATTACK_CLASS_HYPER || getActiveHitDataAttackClass(p) == MUGEN_ATTACK_CLASS_SPECIAL || getActiveHitDataAttackType(p) == MUGEN_ATTACK_TYPE_THROW) return;

	double pushOffsetX;
	if (isPlayerGuarding(p)) {

		if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) {
			pushOffsetX = getActiveAirGuardCornerPushVelocityOffset(p);
		}
		else {
			pushOffsetX = getActiveGuardCornerPushVelocityOffset(p);
		}
	}
	else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) {
		pushOffsetX = getActiveAirCornerPushVelocityOffset(p);
	}
	else  if (getPlayerStateType(p) == MUGEN_STATE_TYPE_LYING) {
		pushOffsetX = getActiveDownCornerPushVelocityOffset(p);
	}
	else {
		pushOffsetX = getActiveGroundCornerPushVelocityOffset(p);
	}

	static const double PUSH_VELOCITY_SCALE = 2.0;
	pushOffsetX += getActiveHitDataVelocityX(p)*PUSH_VELOCITY_SCALE;

	addPlayerVelocityX(tOtherPlayer, pushOffsetX, getPlayerCoordinateP(p));
}

static void setPlayerHit(DreamPlayer* p, DreamPlayer* tOtherPlayer, void* tHitData) {
	setPlayerControl(p, 0);

	copyHitDataToActive(p, tHitData);

	const auto player2ChangeFaceDirectionRelativeToPlayer1 = getActiveHitDataPlayer2ChangeFaceDirectionRelativeToPlayer1(p);
	if (player2ChangeFaceDirectionRelativeToPlayer1) {
		if (player2ChangeFaceDirectionRelativeToPlayer1 > 0) {
			setPlayerIsFacingRight(p, !getActiveHitDataIsFacingRight(p));
		}
		else {
			setPlayerIsFacingRight(p, getActiveHitDataIsFacingRight(p));
		}
	}
	else {
		setPlayerIsFacingRight(p, !getActiveHitDataIsFacingRight(p));
	}

	if (getPlayerUnguardableFlag(tOtherPlayer) || (isPlayerGuarding(p) && !checkPlayerHitGuardFlagsAndReturnIfGuardable(p, getActiveHitDataGuardFlag(p)))) {
		setPlayerUnguarding(p);
	}

	if (isPlayerGuarding(p)) {
		setActiveHitDataVelocityX(p, getActiveHitDataGuardVelocity(p));
		setActiveHitDataVelocityY(p, 0);
		p->mIsFalling = 0; 
	}
	else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) {
		setActiveHitDataVelocityX(p, getActiveHitDataAirVelocityX(p));
	 	setActiveHitDataVelocityY(p, getActiveHitDataAirVelocityY(p));
		p->mIsFalling = getActiveHitDataAirFall(p);
	}
	else {
		setActiveHitDataVelocityX(p, getActiveHitDataGroundVelocityX(p));
		setActiveHitDataVelocityY(p, getActiveHitDataGroundVelocityY(p));
		p->mIsFalling = getActiveHitDataFall(p);
	}

	handlePlayerCornerPush(p, tOtherPlayer);

	int hitShakeDuration, hitPauseDuration;
	int damage;
	int powerUp1;
	int powerUp2;
	if (isPlayerGuarding(p)) {
		damage = (int)(getActiveHitDataGuardDamage(p) * p->mAttackMultiplier);
		powerUp1 = getActiveHitDataPlayer2GuardPowerAdded(p);
		powerUp2 = getActiveHitDataPlayer1GuardPowerAdded(p);
		hitPauseDuration = getActiveHitDataPlayer1GuardPauseTime(p);
		hitShakeDuration = getActiveHitDataPlayer2GuardPauseTime(p);
	}
	else {
		damage = (int)(getActiveHitDataDamage(p) * p->mAttackMultiplier);
		powerUp1 = getActiveHitDataPlayer2PowerAdded(p);
		powerUp2 = getActiveHitDataPlayer1PowerAdded(p);
		hitPauseDuration = getActiveHitDataPlayer1PauseTime(p);
		hitShakeDuration = getActiveHitDataPlayer2PauseTime(p);
	}

	p->mIsHitShakeActive = 1;
	p->mIsHitOver = 0;
	p->mTargetID = getActiveHitDataHitID(p);

	p->mCanRecoverFromFall = getActiveHitDataFallRecovery(p);
	p->mRecoverTime = getActiveHitDataFallRecoveryTime(p);
	p->mRecoverTimeSinceHitPause = 0;

	int isPlayerStateMachinePaused = isDreamRegisteredStateMachinePaused(p->mStateMachineID);
	int isOtherPlayerStateMachinePaused = isDreamRegisteredStateMachinePaused(tOtherPlayer->mStateMachineID);
	unpauseDreamRegisteredStateMachine(p->mStateMachineID);
	unpauseDreamRegisteredStateMachine(tOtherPlayer->mStateMachineID);
	setPlayerHitStates(p, tOtherPlayer);
	setDreamRegisteredStateMachinePauseStatus(tOtherPlayer->mStateMachineID, isOtherPlayerStateMachinePaused);
	setDreamRegisteredStateMachinePauseStatus(p->mStateMachineID, isPlayerStateMachinePaused);

	if (isPlayerGuarding(p)) {
		setPlayerMoveGuarded(tOtherPlayer);
		p->mLastHitGuarded = 1;
	} else {
		setPlayerMoveHit(tOtherPlayer);
		p->mLastHitGuarded = 0;
	}

	addPlayerDamage(getPlayerRoot(p), tOtherPlayer, damage);
	addPlayerPower(getPlayerRoot(p), powerUp1);
	addPlayerPower(tOtherPlayer, powerUp2);

	if (hitShakeDuration && isPlayerAlive(p)) {
		setPlayerHitPaused(p, hitShakeDuration);
	}

	if (hitPauseDuration) {
		setPlayerHitPaused(tOtherPlayer, hitPauseDuration);
	}

	p->mIsHitOverWaitActive = 0;
	p->mHitShakeNow = 0;
	p->mHitShakeDuration = hitShakeDuration;
}

static void playPlayerHitSpark(DreamPlayer* p1, DreamPlayer* p2, int tIsInPlayerFile, int tNumber, Position tSparkOffset) {
	if (tNumber == -1) return;

	Position pos1 = *getHandledPhysicsPositionReference(p1->mPhysicsElement);
	Position pos2 = *getHandledPhysicsPositionReference(p2->mPhysicsElement);

	Position base;
	if (pos1.x < pos2.x) {
		double width = p1->mFaceDirection == FACE_DIRECTION_RIGHT ? getPlayerFrontWidth(p1) : p1->mHeader->mFiles.mConstants.mSizeData.mGroundBackWidth;
		base = vecAdd(pos1, makePosition(width, 0, 0));
	}
	else {
		double width = p1->mFaceDirection == FACE_DIRECTION_LEFT ? getPlayerFrontWidth(p1) : p1->mHeader->mFiles.mConstants.mSizeData.mGroundBackWidth;
		base = vecAdd(pos1, makePosition(-width, 0, 0));
	}
	base.y = pos2.y;

	if (!getActiveHitDataIsFacingRight(p1)) {
		tSparkOffset.x = -tSparkOffset.x;
	}

	tSparkOffset = vecAdd(tSparkOffset, base);

	playDreamHitSpark(tSparkOffset, p2, tIsInPlayerFile, tNumber, getActiveHitDataIsFacingRight(p1), getPlayerCoordinateP(p2), getPlayerCoordinateP(p2));
}

typedef struct {
	int mFound;
	MugenAttackType mType;
	MugenAttackClass mClass;
} HitDefAttributeFlag2Caller;

static void checkSingleFlag2InHitDefAttributeSlot(HitDefAttributeFlag2Caller* tCaller, char* tFlag) {

	if (tCaller->mClass == MUGEN_ATTACK_CLASS_NORMAL) {
		if (tFlag[0] != 'n') return;
	}
	else if (tCaller->mClass == MUGEN_ATTACK_CLASS_SPECIAL) {
		if (tFlag[0] != 's') return;
	}
	else if (tCaller->mClass == MUGEN_ATTACK_CLASS_HYPER) {
		if (tFlag[0] != 'h') return;
	}
	else {
		logWarningFormat("Unrecognized attack class %d. Defaulting to not found.", tCaller->mClass);
		return;
	}

	if (tCaller->mType == MUGEN_ATTACK_TYPE_ATTACK) {
		if (tFlag[1] != 'a') return;
	} 
	else if (tCaller->mType == MUGEN_ATTACK_TYPE_PROJECTILE) {
		if (tFlag[1] != 'p') return;
	}
	else if (tCaller->mType == MUGEN_ATTACK_TYPE_THROW) {
		if (tFlag[1] != 't') return;
	}
	else {
		logWarningFormat("Unrecognized attack type %d. Defaulting to not found.", tCaller->mType);
		return;
	}

	tCaller->mFound = 1;
}

static int checkSingleNoHitDefSlot(DreamHitDefAttributeSlot* tSlot, DreamPlayer* p2) {
	if (!tSlot->mIsActive) return 1;

	DreamMugenStateType type = getHitDataType(p2);

	if (type == MUGEN_STATE_TYPE_STANDING) {
		if (strchr(tSlot->mFlag1, 's') != NULL) return tSlot->mIsHitBy;
	} 
	else if (type == MUGEN_STATE_TYPE_CROUCHING) {
		if (strchr(tSlot->mFlag1, 'c') != NULL) return tSlot->mIsHitBy;
	}
	else if (type == MUGEN_STATE_TYPE_AIR) {
		if (strchr(tSlot->mFlag1, 'a') != NULL) return tSlot->mIsHitBy;
	}
	else {
		logWarningFormat("Invalid hitdef type %d. Defaulting to not not hit.", type);
		return 0;
	}

	HitDefAttributeFlag2Caller caller;
	caller.mFound = 0;
	caller.mType = getHitDataAttackType(p2);
	caller.mClass = getHitDataAttackClass(p2);

	int i;
	for (i = 0; i < tSlot->mFlag2Amount; i++) {
		checkSingleFlag2InHitDefAttributeSlot(&caller, tSlot->mFlag2[i]);
	}

	if (caller.mFound) return tSlot->mIsHitBy;
	else return !tSlot->mIsHitBy;
}

static int checkActiveHitDefAttributeSlots(DreamPlayer* p, DreamPlayer* p2) {
	int i;
	for (i = 0; i < 2; i++) {
		if (!checkSingleNoHitDefSlot(&p->mNotHitBy[i], p2)) return 0;
	}

	return 1;
}

static int isIgnoredBecauseOfJuggle(DreamPlayer* p, DreamPlayer* tOtherPlayer) {
	int isJuggableState;

	if (isPlayerGuarding(p)) {
		isJuggableState = 0;
	}
	else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) {
		setActiveHitDataVelocityX(p, getActiveHitDataAirVelocityX(p));
		setActiveHitDataVelocityY(p, getActiveHitDataAirVelocityY(p));
		isJuggableState = isPlayerFalling(p) || getActiveHitDataAirFall(p);
	}
	else {
		isJuggableState = getPlayerStateType(p) == MUGEN_STATE_TYPE_LYING;
	}

	if (!isJuggableState) return 0;
	if (tOtherPlayer->mNoJuggleCheckFlag) return 0;

	p->mIsBeingJuggled = 1;
	p->mAirJugglePoints -= getPlayerStateJugglePoints(tOtherPlayer);

	return p->mAirJugglePoints < 0;
}

static void playPlayerHitSound(DreamPlayer* p, void(*tFunc)(DreamPlayer*,int*,Vector3DI*)) {
	int isInPlayerFile;
	Vector3DI sound;
	tFunc(p, &isInPlayerFile, &sound);

	MugenSounds* soundFile;
	if (isInPlayerFile) {
		soundFile = getPlayerSounds(p);
	}
	else {
		soundFile = getDreamCommonSounds();
	}

	tryPlayMugenSound(soundFile, sound.x, sound.y);
}

static void increaseDisplayedComboCounter(DreamPlayer* p, int tValue) {
	p->mDisplayedComboCounter += tValue;
	setComboUIDisplay(p->mRootID, p->mDisplayedComboCounter);

}

static void playerHitEval(void* tCaller, void* tHitData) {
	// TODO: reversaldef (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/632)
	DreamPlayer* p = (DreamPlayer*)tCaller;
	DreamPlayer* otherPlayer = getReceivedHitDataPlayer(tHitData);

	if (p->mRootID == otherPlayer->mRootID) return;
	if (isPlayerProjectile(p) && !isPlayerProjectile(otherPlayer)) return;
	if (!isInOwnStateMachine(getPlayerRoot(otherPlayer)->mStateMachineID)) return;
	if (isDreamAnyPauseActive()) return;

	if (!isReceivedHitDataActive(tHitData)) return;
	if (!checkActiveHitDefAttributeSlots(p, otherPlayer)) return;
	if (isIgnoredBecauseOfHitOverride(p, otherPlayer)) return;
	if (isIgnoredBecauseOfJuggle(p, otherPlayer)) return;
	if (getDreamTimeSinceKO() >= getOverHitTime()) return;

	setPlayerHit(p, otherPlayer, tHitData);

	const auto playerGuarding = isPlayerGuarding(p);
	if (playerGuarding) {
		playPlayerHitSound(p, getActiveHitDataGuardSound);
		playPlayerHitSpark(p, otherPlayer, isActiveHitDataGuardSparkInPlayerFile(p), getActiveHitDataGuardSparkNumber(p), getActiveHitDataSparkXY(p));
	}
	else {
		increasePlayerHitCount(otherPlayer);
		increaseDisplayedComboCounter(getPlayerOtherPlayer(p), 1);
		playPlayerHitSound(p, getActiveHitDataHitSound);
		playPlayerHitSpark(p, otherPlayer, isActiveHitDataSparkInPlayerFile(p), getActiveHitDataSparkNumber(p), getActiveHitDataSparkXY(p));
		setEnvironmentShake(getActiveHitDataEnvironmentShakeTime(p), getActiveHitDataEnvironmentShakeFrequency(p), getActiveHitDataEnvironmentShakeAmplitude(p), getActiveHitDataEnvironmentShakePhase(p));
	}

	setPlayerMoveContactCounterActive(otherPlayer);

	if (isPlayerProjectile(p)) {
		handleProjectileHit(p, 0, isPlayerProjectile(otherPlayer));
	}

	if (isPlayerProjectile(otherPlayer)) {
		handleProjectileHit(otherPlayer, playerGuarding, isPlayerProjectile(p));
	}
}

static void updatePlayerReceivedHits(DreamPlayer* p) {
	list_map(&p->mReceivedHitData, playerHitEval, p);
	list_empty(&p->mReceivedHitData);
}

void playerHitCB(void* tData, void* tHitData)
{
	DreamPlayer* p = (DreamPlayer*)tData;
	if (p->mIsDestroyed) return;

	PlayerHitData* receivedHitData = (PlayerHitData*)tHitData;
	PlayerHitData* copyHitData = (PlayerHitData*)allocMemory(sizeof(PlayerHitData));
	*copyHitData = *receivedHitData;
	setReceivedHitDataInactive(tHitData);
	list_push_back_owned(&p->mReceivedHitData, copyHitData);
}

void setPlayerDefinitionPath(int i, const char * tDefinitionPath)
{
	strcpy(gPlayerDefinition.mPlayerHeader[i].mFiles.mDefinitionPath, tDefinitionPath);
}

void getPlayerDefinitionPath(char* tDst, int i)
{
	strcpy(tDst, gPlayerDefinition.mPlayerHeader[i].mFiles.mDefinitionPath);
}

void setPlayerPreferredPalette(int i, int tPalette)
{
	gPlayerDefinition.mPlayers[i].mPreferredPalette = tPalette;
}

DreamPlayer * getRootPlayer(int i)
{
	return &gPlayerDefinition.mPlayers[i];
}

DreamPlayer * getPlayerRoot(DreamPlayer * p)
{
	return p->mRoot;
}

DreamPlayer * getPlayerParent(DreamPlayer * p)
{
	if (!p->mParent) {
		logWarningFormat("%d %d trying to access parents as root. Returning self.\n", p->mRootID, p->mID);
		return p;
	}
	return p->mParent;
}

int getPlayerState(DreamPlayer* p)
{
	return getDreamRegisteredStateState(p->mStateMachineID);
}

int getPlayerPreviousState(DreamPlayer* p)
{
	return getDreamRegisteredStatePreviousState(p->mStateMachineID);
}

int getPlayerStateJugglePoints(DreamPlayer * p)
{
	return getDreamRegisteredStateJugglePoints(p->mStateMachineID);;
}

DreamMugenStateType getPlayerStateType(DreamPlayer* p)
{
	return p->mStateType;
}

void setPlayerStateType(DreamPlayer* p, DreamMugenStateType tType)
{
	if (tType == MUGEN_STATE_TYPE_UNCHANGED) {
		return;
	}

	p->mStateType = tType;
}

DreamMugenStateMoveType getPlayerStateMoveType(DreamPlayer* p)
{
	return p->mMoveType;
}

void setPlayerStateMoveType(DreamPlayer* p, DreamMugenStateMoveType tType)
{
	if (tType == MUGEN_STATE_MOVE_TYPE_UNCHANGED) return;

	p->mMoveType = tType;
}

int getPlayerControl(DreamPlayer* p)
{
	return p->mIsInControl;
}

void setPlayerControl(DreamPlayer* p, int tNewControl)
{
	p->mIsInControl = tNewControl;
}


DreamMugenStatePhysics getPlayerPhysics(DreamPlayer* p) {
	return p->mStatePhysics;
}

void setPlayerPhysics(DreamPlayer* p, DreamMugenStatePhysics tNewPhysics)
{
	if (tNewPhysics == MUGEN_STATE_PHYSICS_UNCHANGED) {
		return;
	}
	else if (tNewPhysics == MUGEN_STATE_PHYSICS_STANDING) {
		setHandledPhysicsDragCoefficient(p->mPhysicsElement, makePosition(p->mHeader->mFiles.mConstants.mMovementData.mStandFiction, 0, 0));
		setHandledPhysicsGravity(p->mPhysicsElement, makePosition(0, 0, 0));
		Velocity* vel = getHandledPhysicsVelocityReference(p->mPhysicsElement);
		Acceleration* acc = getHandledPhysicsAccelerationReference(p->mPhysicsElement);
		
		vel->y = 0;
		acc->y = 0;
	}
	else if (tNewPhysics == MUGEN_STATE_PHYSICS_CROUCHING) {
		setHandledPhysicsDragCoefficient(p->mPhysicsElement, makePosition(p->mHeader->mFiles.mConstants.mMovementData.mCrouchFriction, 0, 0));
		setHandledPhysicsGravity(p->mPhysicsElement, makePosition(0, 0, 0));
		Position* pos = getHandledPhysicsPositionReference(p->mPhysicsElement);
		Velocity* vel = getHandledPhysicsVelocityReference(p->mPhysicsElement);
		Acceleration* acc = getHandledPhysicsAccelerationReference(p->mPhysicsElement);

		pos->y = 0;
		vel->y = 0;
		acc->y = 0;
	}
	else if (tNewPhysics == MUGEN_STATE_PHYSICS_NONE) {
		setHandledPhysicsDragCoefficient(p->mPhysicsElement, makePosition(0, 0, 0));
		setHandledPhysicsGravity(p->mPhysicsElement, makePosition(0, 0, 0));


		if (getPlayerStateType(p) == MUGEN_STATE_TYPE_LYING) {
			Acceleration* acc = getHandledPhysicsAccelerationReference(p->mPhysicsElement);
			acc->y = 0;
		}
	}
	else if (tNewPhysics == MUGEN_STATE_PHYSICS_AIR) {
		setHandledPhysicsDragCoefficient(p->mPhysicsElement, makePosition(0, 0, 0));
		setHandledPhysicsGravity(p->mPhysicsElement, makePosition(0, p->mHeader->mFiles.mConstants.mMovementData.mVerticalAcceleration, 0));

		if (tNewPhysics != p->mStatePhysics) {
			setPlayerNoLandFlag(p);			
		}

		if (p->mStatePhysics == MUGEN_STATE_PHYSICS_STANDING || p->mStatePhysics == MUGEN_STATE_PHYSICS_CROUCHING) {
			p->mAirJumpCounter = 0;
			p->mJumpFlank = 0;
		}
	}
	else {
		logWarningFormat("Unrecognized physics state %d. Defaulting to unchanged.", tNewPhysics);
		return;
	}

	p->mStatePhysics = tNewPhysics;
}

int getPlayerMoveContactCounter(DreamPlayer* p)
{
	return p->mMoveContactCounter;
}

static void increaseComboCounter(DreamPlayer* p, int tAmount) {

	p->mComboCounter += tAmount;
}

static void resetComboCounter(DreamPlayer* p) {
	if (!getPlayerControl(p)) return;

	p->mComboCounter = 0;
}

void resetPlayerMoveContactCounter(DreamPlayer* p)
{
	p->mMoveContactCounter = 0;
	resetComboCounter(p);
}

void setPlayerMoveContactCounterActive(DreamPlayer* p) {
	p->mMoveContactCounter = 1;
	increaseComboCounter(p, 1);
}

int getPlayerVariable(DreamPlayer* p, int tIndex)
{
	return p->mVars[tIndex];
}

int * getPlayerVariableReference(DreamPlayer * p, int tIndex)
{
	return &p->mVars[tIndex];
}

void setPlayerVariable(DreamPlayer* p, int tIndex, int tValue)
{
	p->mVars[tIndex] = tValue;
}

void addPlayerVariable(DreamPlayer * p, int tIndex, int tValue)
{
	int cur = getPlayerVariable(p, tIndex);
	cur += tValue;
	setPlayerVariable(p, tIndex, cur);
}

int getPlayerSystemVariable(DreamPlayer* p, int tIndex)
{
	return p->mSystemVars[tIndex];
}

void setPlayerSystemVariable(DreamPlayer* p, int tIndex, int tValue)
{
	p->mSystemVars[tIndex] = tValue;
}

void addPlayerSystemVariable(DreamPlayer * p, int tIndex, int tValue)
{
	int cur = getPlayerSystemVariable(p, tIndex);
	cur += tValue;
	setPlayerSystemVariable(p, tIndex, cur);
}

double getPlayerFloatVariable(DreamPlayer* p, int tIndex)
{
	return p->mFloatVars[tIndex];
}

void setPlayerFloatVariable(DreamPlayer* p, int tIndex, double tValue)
{
	p->mFloatVars[tIndex] = tValue;
}

void addPlayerFloatVariable(DreamPlayer * p, int tIndex, double tValue)
{
	double cur = getPlayerFloatVariable(p, tIndex);
	cur += tValue;
	setPlayerFloatVariable(p, tIndex, cur);
}

double getPlayerSystemFloatVariable(DreamPlayer* p, int tIndex)
{
	return p->mSystemFloatVars[tIndex];
}

void setPlayerSystemFloatVariable(DreamPlayer* p, int tIndex, double tValue)
{
	p->mSystemFloatVars[tIndex] = tValue;
}

void addPlayerSystemFloatVariable(DreamPlayer * p, int tIndex, double tValue)
{
	double cur = getPlayerSystemFloatVariable(p, tIndex);
	cur += tValue;
	setPlayerSystemFloatVariable(p, tIndex, cur);
}

int getPlayerTimeInState(DreamPlayer* p)
{
	return getDreamRegisteredStateTimeInState(p->mStateMachineID);
}

int getPlayerAnimationNumber(DreamPlayer* p)
{
	return getMugenAnimationAnimationNumber(p->mAnimationElement);
}

int getPlayerAnimationStep(DreamPlayer* p) {
	return getMugenAnimationAnimationStep(p->mAnimationElement);
}

int getPlayerAnimationStepAmount(DreamPlayer* p) {
	return getMugenAnimationAnimationStepAmount(p->mAnimationElement);
}

int getPlayerAnimationStepDuration(DreamPlayer * p)
{
	return getMugenAnimationAnimationStepDuration(p->mAnimationElement);
}

int getPlayerAnimationTimeDeltaUntilFinished(DreamPlayer* p)
{
	return -getMugenAnimationRemainingAnimationTime(p->mAnimationElement);
}

int hasPlayerAnimationLooped(DreamPlayer* p)
{
	return hasMugenAnimationLooped(p->mAnimationElement);
}

int getPlayerAnimationDuration(DreamPlayer* p) {
	return getMugenAnimationDuration(p->mAnimationElement);
}

int getPlayerAnimationTime(DreamPlayer* p) {
	return getMugenAnimationTime(p->mAnimationElement);
}

int getPlayerSpriteGroup(DreamPlayer* p) {
	return getMugenAnimationSprite(p->mAnimationElement).x;
}

int getPlayerSpriteElement(DreamPlayer* p) {
	return getMugenAnimationSprite(p->mAnimationElement).y;
}

Vector3D getPlayerPosition(DreamPlayer* p, int tCoordinateP)
{
	return makePosition(getPlayerPositionX(p, tCoordinateP), getPlayerPositionY(p, tCoordinateP), 0);
}

double getPlayerPositionBasedOnScreenCenterX(DreamPlayer * p, int tCoordinateP)
{
	Position pos = getDreamStageCenterOfScreenBasedOnPlayer(tCoordinateP);
	Position ret = vecSub(getPlayerPosition(p, tCoordinateP), pos);
	return ret.x;
}

double getPlayerScreenPositionX(DreamPlayer * p, int tCoordinateP)
{
	double scale = tCoordinateP / getPlayerCoordinateP(p);
	double physicsPosition = getHandledPhysicsPositionReference(p->mPhysicsElement)->x;
	double cameraPosition = getDreamMugenStageHandlerCameraPositionReference()->x;
	double animationPosition = getMugenAnimationPosition(p->mAnimationElement).x;
	double unscaledPosition = physicsPosition + animationPosition - cameraPosition;
	return unscaledPosition * scale;
}

double getPlayerPositionX(DreamPlayer* p, int tCoordinateP)
{
	double scale = tCoordinateP / getPlayerCoordinateP(p);
	double val = getHandledPhysicsPositionReference(p->mPhysicsElement)->x * scale;
	return val;
}

double getPlayerPositionBasedOnStageFloorY(DreamPlayer * p, int tCoordinateP)
{
	Position ret = getPlayerPosition(p, tCoordinateP);
	return ret.y;
}

double getPlayerScreenPositionY(DreamPlayer * p, int tCoordinateP)
{
	double scale = tCoordinateP / getPlayerCoordinateP(p);
	double physicsPosition = getHandledPhysicsPositionReference(p->mPhysicsElement)->y;
	double cameraPosition = getDreamMugenStageHandlerCameraPositionReference()->y;
	double animationPosition = getMugenAnimationPosition(p->mAnimationElement).y;
	double unscaledPosition = physicsPosition + animationPosition - cameraPosition;
	return unscaledPosition * scale;
}

double getPlayerPositionY(DreamPlayer* p, int tCoordinateP)
{
	double scale = tCoordinateP / getPlayerCoordinateP(p);
	return getHandledPhysicsPositionReference(p->mPhysicsElement)->y * scale;
}

double getPlayerVelocityX(DreamPlayer* p, int tCoordinateP)
{
	double scale = tCoordinateP / getPlayerCoordinateP(p);
	double x = getHandledPhysicsVelocityReference(p->mPhysicsElement)->x * scale;
	if (p->mFaceDirection == FACE_DIRECTION_LEFT) x *= -1;

	return  x;
}

double getPlayerVelocityY(DreamPlayer* p, int tCoordinateP)
{
	double scale = tCoordinateP / getPlayerCoordinateP(p);
	return getHandledPhysicsVelocityReference(p->mPhysicsElement)->y * scale;
}

int getPlayerDataLife(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mLife;
}

int getPlayerDataAttack(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mAttack;
}

int getPlayerDataDefense(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mDefense;
}

int getPlayerDataLiedownTime(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mLiedownTime;
}

int getPlayerDataAirjuggle(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mAirJugglePoints;
}

int getPlayerDataSparkNo(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mSparkNo;
}

int getPlayerDataGuardSparkNo(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mGuardSparkNo;
}

int getPlayerDataKOEcho(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mKOEcho;
}

int getPlayerDataIntPersistIndex(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mIntPersistIndex;
}

int getPlayerDataFloatPersistIndex(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mFloatPersistIndex;
}

int getPlayerSizeAirBack(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mAirBackWidth;
}

int getPlayerSizeAirFront(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mAirFrontWidth;
}

int getPlayerSizeAttackDist(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mAttackDistance;
}

int getPlayerSizeProjectileAttackDist(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mProjectileAttackDistance;
}

int getPlayerSizeProjectilesDoScale(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mDoesScaleProjectiles;
}

int getPlayerSizeShadowOffset(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mShadowOffset;
}

int getPlayerSizeDrawOffsetX(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mDrawOffset.x;
}

int getPlayerSizeDrawOffsetY(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mDrawOffset.y;
}

double getPlayerVelocityAirGetHitGroundRecoverX(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirGetHitGroundRecovery.x;
}

double getPlayerVelocityAirGetHitGroundRecoverY(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirGetHitGroundRecovery.y;
}

double getPlayerVelocityAirGetHitAirRecoverMulX(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirGetHitAirRecoveryMultiplier.x;
}

double getPlayerVelocityAirGetHitAirRecoverMulY(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirGetHitAirRecoveryMultiplier.y;
}

double getPlayerVelocityAirGetHitAirRecoverAddX(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirGetHitAirRecoveryOffset.x;
}

double getPlayerVelocityAirGetHitAirRecoverAddY(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirGetHitAirRecoveryOffset.y;
}

double getPlayerVelocityAirGetHitAirRecoverBack(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirGetHitExtraXWhenHoldingBackward;
}

double getPlayerVelocityAirGetHitAirRecoverFwd(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirGetHitExtraXWhenHoldingForward;
}

double getPlayerVelocityAirGetHitAirRecoverUp(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirGetHitExtraYWhenHoldingUp;
}

double getPlayerVelocityAirGetHitAirRecoverDown(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirGetHitExtraYWhenHoldingDown;
}

int getPlayerMovementAirJumpNum(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mAllowedAirJumpAmount;
}

int getPlayerMovementAirJumpHeight(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mAirJumpMinimumHeight;
}

double getPlayerMovementJumpChangeAnimThreshold(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mJumpChangeAnimThreshold;
}

double getPlayerMovementAirGetHitAirRecoverYAccel(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mAirGetHitAirRecoveryVerticalAcceleration;
}

double getPlayerStandFriction(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mStandFiction;
}

double getPlayerStandFrictionThreshold(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mStandFrictionThreshold;
}

double getPlayerCrouchFriction(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mCrouchFriction;
}

double getPlayerCrouchFrictionThreshold(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mCrouchFrictionThreshold;
}

double getPlayerAirGetHitGroundLevelY(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mAirGetHitGroundLevelY;
}

double getPlayerAirGetHitGroundRecoveryGroundLevelY(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mAirGetHitGroundRecoveryGroundGoundLevelY;
}

double getPlayerAirGetHitGroundRecoveryGroundYTheshold(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mAirGetHitGroundRecoveryGroundYTheshold;
}

double getPlayerAirGetHitAirRecoveryVelocityYThreshold(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mAirGetHitAirRecoveryVelocityYThreshold;
}

double getPlayerAirGetHitTripGroundLevelY(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mAirGetHitTripGroundLevelY;
}

double getPlayerDownBounceOffsetX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mBounceOffset.x;
}

double getPlayerDownBounceOffsetY(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mBounceOffset.y;
}

double getPlayerDownVerticalBounceAcceleration(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mVerticalBounceAcceleration;
}

double getPlayerDownBounceGroundLevel(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mBounceGroundLevel;
}

double getPlayerLyingDownFrictionThreshold(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mLyingDownFrictionThreshold;
}

double getPlayerVerticalAcceleration(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mMovementData.mVerticalAcceleration;
}

double getPlayerForwardWalkVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mWalkForward.x;
}

double getPlayerBackwardWalkVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mWalkBackward.x;
}

double getPlayerForwardRunVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mRunForward.x;
}

double getPlayerForwardRunVelocityY(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mRunForward.y;
}

double getPlayerBackwardRunVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mRunBackward.x;
}

double getPlayerBackwardRunVelocityY(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mRunBackward.y;
}

double getPlayerBackwardRunJumpVelocityX(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mRunJumpBackward.x;
}

double getPlayerForwardRunJumpVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mRunJumpForward.x;
}

double getPlayerNeutralJumpVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mJumpNeutral.x;
}

double getPlayerForwardJumpVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mJumpForward.x;
}

double getPlayerBackwardJumpVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mJumpBackward.x;
}

double getPlayerJumpVelocityY(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mJumpNeutral.y;
}

double getPlayerNeutralAirJumpVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirJumpNeutral.x;
}

double getPlayerForwardAirJumpVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirJumpForward.x;
}

double getPlayerBackwardAirJumpVelocityX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirJumpBackward.x;
}

double getPlayerAirJumpVelocityY(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mVelocityData.mAirJumpNeutral.y;
}


int isPlayerAlive(DreamPlayer* p)
{
	return p->mIsAlive;
}

int isPlayerDestroyed(DreamPlayer * p)
{
	return p->mIsDestroyed;
}

void setPlayerVelocityX(DreamPlayer* p, double x, int tCoordinateP)
{
	double scale = getPlayerCoordinateP(p) / tCoordinateP;
	Velocity* vel = getHandledPhysicsVelocityReference(p->mPhysicsElement);
	double fx = x*scale;
	if (p->mFaceDirection == FACE_DIRECTION_LEFT) fx *= -1;
	vel->x = fx;
}

void setPlayerVelocityY(DreamPlayer* p, double y, int tCoordinateP)
{
	double scale = getPlayerCoordinateP(p) / tCoordinateP;
	Velocity* vel = getHandledPhysicsVelocityReference(p->mPhysicsElement);
	vel->y = y*scale;
}

void multiplyPlayerVelocityX(DreamPlayer* p, double x, int tCoordinateP)
{
	double scale = getPlayerCoordinateP(p) / tCoordinateP;
	Velocity* vel = getHandledPhysicsVelocityReference(p->mPhysicsElement);
	double fx = x * scale;
	vel->x *= fx;
}

void multiplyPlayerVelocityY(DreamPlayer* p, double y, int tCoordinateP)
{
	double scale = getPlayerCoordinateP(p) / tCoordinateP;
	Velocity* vel = getHandledPhysicsVelocityReference(p->mPhysicsElement);
	vel->y *= y*scale;
}

void addPlayerVelocityX(DreamPlayer* p, double x, int tCoordinateP)
{
	if (isPlayerPaused(p)) return;

	double scale = getPlayerCoordinateP(p) / tCoordinateP;
	Velocity* vel = getHandledPhysicsVelocityReference(p->mPhysicsElement);
	double fx = x*scale;
	if (p->mFaceDirection == FACE_DIRECTION_LEFT) fx *= -1;
	vel->x += fx;
}

void addPlayerVelocityY(DreamPlayer* p, double y, int tCoordinateP)
{
	if (isPlayerPaused(p)) return;

	double scale = getPlayerCoordinateP(p) / tCoordinateP;
	Velocity* vel = getHandledPhysicsVelocityReference(p->mPhysicsElement);
	vel->y += y*scale;
}

void setPlayerPosition(DreamPlayer * p, Position tPosition, int tCoordinateP)
{
	setPlayerPositionX(p, tPosition.x, tCoordinateP);
	setPlayerPositionY(p, tPosition.y, tCoordinateP);
}

void setPlayerPositionX(DreamPlayer* p, double x, int tCoordinateP)
{
	double scale = getPlayerCoordinateP(p) / tCoordinateP;
	Position* pos = getHandledPhysicsPositionReference(p->mPhysicsElement);
	pos->x = x*scale;
}

void setPlayerPositionY(DreamPlayer* p, double y, int tCoordinateP)
{
	double scale = getPlayerCoordinateP(p) / tCoordinateP;
	Position* pos = getHandledPhysicsPositionReference(p->mPhysicsElement);
	pos->y = y*scale;
}

void addPlayerPositionX(DreamPlayer* p, double x, int tCoordinateP)
{
	if (isPlayerPaused(p)) return;

	double scale = getPlayerCoordinateP(p) / tCoordinateP;
	Position* pos = getHandledPhysicsPositionReference(p->mPhysicsElement);
	double fx = x*scale;
	if (p->mFaceDirection == FACE_DIRECTION_LEFT) fx *= -1;
	pos->x += fx;
}

void addPlayerPositionY(DreamPlayer* p, double y, int tCoordinateP)
{
	if (isPlayerPaused(p)) return;

	Position* pos = getHandledPhysicsPositionReference(p->mPhysicsElement);

	double scale = getPlayerCoordinateP(p) / tCoordinateP;
	pos->y += y*scale;
}

void setPlayerPositionBasedOnScreenCenterX(DreamPlayer* p, double x, int tCoordinateP) {
	Position pos = getDreamStageCenterOfScreenBasedOnPlayer(tCoordinateP);
	double nx = x + pos.x;
	setPlayerPositionX(p, nx, tCoordinateP);
}

int isPlayerCommandActive(DreamPlayer* p, const char * tCommandName)
{
	return isDreamCommandActive(p->mCommandID, tCommandName);
}

int isPlayerCommandActiveWithLookup(DreamPlayer * p, int tCommandLookupIndex)
{
	return isDreamCommandActiveByLookupIndex(p->mCommandID, tCommandLookupIndex);
}



int hasPlayerState(DreamPlayer * p, int mNewState)
{
	return hasDreamHandledStateMachineState(p->mStateMachineID, mNewState);
}

int hasPlayerStateSelf(DreamPlayer * p, int mNewState)
{
	return hasDreamHandledStateMachineStateSelf(p->mStateMachineID, mNewState);
}

void changePlayerState(DreamPlayer* p, int mNewState)
{
	if (getPlayerControl(p) && !isInOwnStateMachine(p->mStateMachineID)) {
		changeDreamHandledStateMachineStateToOwnStateMachineWithoutChangingState(p->mStateMachineID);
	}

	if (!hasPlayerState(p, mNewState)) {
		logWarning("Trying to change into state that does not exist");
		logWarningInteger(getPlayerState(p));
		logWarningInteger(mNewState);
		return;
	}
	changeDreamHandledStateMachineState(p->mStateMachineID, mNewState);
	setDreamRegisteredStateTimeInState(p->mStateMachineID, -1);
	updateDreamSingleStateMachineByID(p->mStateMachineID);
}

void changePlayerStateToSelf(DreamPlayer * p, int mNewState)
{
	changeDreamHandledStateMachineStateToOwnStateMachineWithoutChangingState(p->mStateMachineID);
	changePlayerState(p, mNewState);
}

void changePlayerStateToOtherPlayerStateMachine(DreamPlayer * p, DreamPlayer * tOtherPlayer, int mNewState)
{
	changeDreamHandledStateMachineStateToOtherPlayerStateMachine(p->mStateMachineID, tOtherPlayer->mStateMachineID, mNewState);
	setDreamRegisteredStateTimeInState(p->mStateMachineID, -1);
	updateDreamSingleStateMachineByID(p->mStateMachineID);
}

void changePlayerStateToOtherPlayerStateMachineBeforeImmediatelyEvaluatingIt(DreamPlayer * p, DreamPlayer * tOtherPlayer, int mNewState)
{
	changeDreamHandledStateMachineStateToOtherPlayerStateMachine(p->mStateMachineID, tOtherPlayer->mStateMachineID, mNewState);
}

void changePlayerStateBeforeImmediatelyEvaluatingIt(DreamPlayer * p, int mNewState)
{
	if (getPlayerControl(p) && !isInOwnStateMachine(p->mStateMachineID)) {
		changeDreamHandledStateMachineStateToOwnStateMachineWithoutChangingState(p->mStateMachineID);
	}

	if (!hasPlayerState(p, mNewState)) {
		logWarning("Trying to change into state that does not exist");
		logWarningInteger(getPlayerState(p));
		logWarningInteger(mNewState);
		return;
	}
	changeDreamHandledStateMachineState(p->mStateMachineID, mNewState);

}

void changePlayerStateToSelfBeforeImmediatelyEvaluatingIt(DreamPlayer * p, int tNewState)
{
	changeDreamHandledStateMachineStateToOwnStateMachine(p->mStateMachineID, tNewState);
}

void changePlayerStateIfDifferent(DreamPlayer* p, int tNewState)
{
	if(getPlayerState(p) == tNewState) return;
	changePlayerState(p, tNewState);
}

void setPlayerStatemachineToUpdateAgain(DreamPlayer * p)
{
	setDreamSingleStateMachineToUpdateAgainByID(p->mStateMachineID);
}

void changePlayerAnimation(DreamPlayer* p, int tNewAnimation)
{
	changePlayerAnimationWithStartStep(p, tNewAnimation, 0);
}

void changePlayerAnimationWithStartStep(DreamPlayer* p, int tNewAnimation, int tStartStep)
{
	p->mActiveAnimations = &p->mHeader->mFiles.mAnimations;
	MugenAnimation* newAnimation = getMugenAnimation(&p->mHeader->mFiles.mAnimations, tNewAnimation);
	changeMugenAnimationWithStartStep(p->mAnimationElement, newAnimation, tStartStep);
	changeMugenAnimationWithStartStep(p->mShadow.mAnimationElement, newAnimation, tStartStep);
	changeMugenAnimationWithStartStep(p->mReflection.mAnimationElement, newAnimation, tStartStep);
}

void changePlayerAnimationToPlayer2AnimationWithStartStep(DreamPlayer * p, int tNewAnimation, int tStartStep)
{
	if (isInOwnStateMachine(p->mStateMachineID)) {
		changePlayerAnimationWithStartStep(p, tNewAnimation, tStartStep);
		return;
	}

	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p);
	p->mActiveAnimations = &otherPlayer->mHeader->mFiles.mAnimations;
	MugenAnimation* newAnimation = getMugenAnimation(&otherPlayer->mHeader->mFiles.mAnimations, tNewAnimation);
	changeMugenAnimationWithStartStep(p->mAnimationElement, newAnimation, tStartStep);
	changeMugenAnimationWithStartStep(p->mShadow.mAnimationElement, newAnimation, tStartStep);
	changeMugenAnimationWithStartStep(p->mReflection.mAnimationElement, newAnimation, tStartStep);
}

void setPlayerAnimationFinishedCallback(DreamPlayer * p, void(*tFunc)(void *), void * tCaller)
{
	setMugenAnimationCallback(p->mAnimationElement, tFunc, tCaller);
}

int isPlayerStartingAnimationElementWithID(DreamPlayer* p, int tStepID)
{
	return isStartingMugenAnimationElementWithID(p->mAnimationElement, tStepID);
}

int getPlayerTimeFromAnimationElement(DreamPlayer* p, int tStep)
{
	return getTimeFromMugenAnimationElement(p->mAnimationElement, tStep);
}

int getPlayerAnimationElementFromTimeOffset(DreamPlayer* p, int tTime)
{
	return getMugenAnimationElementFromTimeOffset(p->mAnimationElement, tTime);
}

int isPlayerAnimationTimeOffsetInAnimation(DreamPlayer* p, int tTime) {
	
	return isMugenAnimationTimeOffsetInAnimation(p->mAnimationElement, tTime);
}

int getPlayerAnimationTimeWhenStepStarts(DreamPlayer* p, int tStep) {
	return getMugenAnimationTimeWhenStepStarts(p->mAnimationElement, tStep);
}

void setPlayerSpritePriority(DreamPlayer* p, int tPriority)
{
	Position pos = getMugenAnimationPosition(p->mAnimationElement);
	pos.z = PLAYER_Z + tPriority * PLAYER_Z_PRIORITY_DELTA + p->mRootID * PLAYER_Z_PLAYER_2_OFFSET;
	setMugenAnimationPosition(p->mAnimationElement, pos);
}

void setPlayerNoWalkFlag(DreamPlayer* p)
{
	p->mNoWalkFlag = 1;
}

void setPlayerNoAutoTurnFlag(DreamPlayer* p)
{
	p->mNoAutoTurnFlag = 1;
}

void setPlayerInvisibleFlag(DreamPlayer * p)
{
	setMugenAnimationInvisibleForOneFrame(p->mAnimationElement); 
	setMugenAnimationInvisibleForOneFrame(p->mShadow.mAnimationElement);
	setMugenAnimationInvisibleForOneFrame(p->mReflection.mAnimationElement);
	p->mInvisibilityFlag = 1;
}

void setPlayerNoLandFlag(DreamPlayer* p)
{
	p->mNoLandFlag = 1;
}

void setPlayerNoShadow(DreamPlayer * p)
{
	setMugenAnimationInvisibleForOneFrame(p->mShadow.mAnimationElement);
}

void setAllPlayersNoShadow()
{
	setPlayerNoShadow(getRootPlayer(0));
	setPlayerNoShadow(getRootPlayer(1));
}

void setPlayerPushDisabledFlag(DreamPlayer * p, int tIsDisabled)
{
	p->mPushDisabledFlag = tIsDisabled;
}

void setPlayerNoJuggleCheckFlag(DreamPlayer * p)
{
	p->mNoJuggleCheckFlag = 1;
}

void setPlayerIntroFlag(DreamPlayer * p)
{
	p->mIntroFlag = 1; // TODO: use (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/293)
}

void setPlayerNoAirGuardFlag(DreamPlayer * p)
{
	p->mNoAirGuardFlag = 1;
}

void setPlayerNoCrouchGuardFlag(DreamPlayer * p)
{
	p->mNoCrouchGuardFlag = 1;

}

void setPlayerNoStandGuardFlag(DreamPlayer * p)
{
	p->mNoStandGuardFlag = 1;
}

void setPlayerNoKOSoundFlag(DreamPlayer * p)
{
	p->mNoKOSoundFlag = 1;
}

void setPlayerNoKOSlowdownFlag(DreamPlayer * p)
{
	p->mNoKOSlowdownFlag = 1;
}

void setPlayerUnguardableFlag(DreamPlayer * p)
{
	p->mUnguardableFlag = 1;
}

int getPlayerNoKOSlowdownFlag(DreamPlayer * p)
{
	return p->mNoKOSlowdownFlag;
}

int getPlayerUnguardableFlag(DreamPlayer * p)
{
	return p->mUnguardableFlag;
}

int isPlayerInIntro(DreamPlayer * p)
{
	int ret = p->mIntroFlag;
	p->mIntroFlag = 0;
	return ret;
}

int doesPlayerHaveAnimation(DreamPlayer * p, int tAnimation)
{
	return hasMugenAnimation(p->mActiveAnimations, tAnimation);
}

int doesPlayerHaveAnimationHimself(DreamPlayer* p, int tAnimation)
{
	return hasMugenAnimation(&p->mHeader->mFiles.mAnimations, tAnimation);
}

int isPlayerHitShakeOver(DreamPlayer* p)
{
	return !p->mIsHitShakeActive;
}

int isPlayerHitOver(DreamPlayer* p)
{
	return p->mIsHitOver;
}

int getPlayerHitTime(DreamPlayer * p)
{
	if (!p->mIsHitShakeActive) return -1;
	return p->mHitShakeDuration - p->mHitShakeNow;
}

double getPlayerHitVelocityX(DreamPlayer * p, int tCoordinateP)
{
	if (isPlayerHitOver(p)) return 0.0;
	return -getPlayerVelocityX(p, tCoordinateP);	
}

double getPlayerHitVelocityY(DreamPlayer * p, int tCoordinateP)
{
	if (isPlayerHitOver(p)) return 0.0;
	return -getPlayerVelocityY(p, tCoordinateP);
}

int isPlayerFalling(DreamPlayer* p) {
	return p->mIsFalling;
}

int canPlayerRecoverFromFalling(DreamPlayer* p)
{
	return isPlayerFalling(p) && p->mCanRecoverFromFall && p->mRecoverTimeSinceHitPause >= p->mRecoverTime;
}

int getPlayerSlideTime(DreamPlayer* p)
{
	return isPlayerGuarding(p) ? getActiveHitDataGuardSlideTime(p) : getActiveHitDataGroundSlideTime(p);
}
 
double getPlayerDefenseMultiplier(DreamPlayer * p)
{
	return p->mDefenseMultiplier;
}

void setPlayerDefenseMultiplier(DreamPlayer* p, double tValue)
{
	p->mDefenseMultiplier = tValue;
}

void setPlayerPositionFrozen(DreamPlayer* p)
{
	p->mIsFrozen = 1;
	p->mFreezePosition = *getHandledPhysicsPositionReference(p->mPhysicsElement);
}

void setPlayerPositionUnfrozen(DreamPlayer* p)
{
	p->mIsFrozen = 0;
}

MugenSpriteFile * getPlayerSprites(DreamPlayer* p)
{
	return &p->mHeader->mFiles.mSprites;
}

MugenAnimations * getPlayerAnimations(DreamPlayer* p)
{
	return &p->mHeader->mFiles.mAnimations;
}

MugenAnimation * getPlayerAnimation(DreamPlayer* p, int tNumber)
{
	return getMugenAnimation(&p->mHeader->mFiles.mAnimations, tNumber);
}

MugenSounds * getPlayerSounds(DreamPlayer * p)
{
	return &p->mHeader->mFiles.mSounds;
}

void setCustomPlayerDisplayName(int i, const std::string tName)
{
	auto header = &gPlayerDefinition.mPlayerHeader[i];
	strcpy(header->mCustomOverrides.mDisplayName, tName.c_str());
	header->mCustomOverrides.mHasCustomDisplayName = 1;
}

int getPlayerCoordinateP(DreamPlayer* p)
{
	return p->mHeader->mConstants.mLocalCoordinates.y;
}

char * getPlayerDisplayName(DreamPlayer* p)
{
	return p->mHeader->mCustomOverrides.mHasCustomDisplayName ? p->mHeader->mCustomOverrides.mDisplayName : p->mHeader->mConstants.mDisplayName;
}

char * getPlayerName(DreamPlayer * p)
{
	return p->mHeader->mConstants.mName;
}

char * getPlayerAuthorName(DreamPlayer * p)
{
	return p->mHeader->mConstants.mAuthor;
}

int isPlayerPaused(DreamPlayer* p)
{
	return p->mIsHitPaused || isDreamAnyPauseActive();
}

static void pausePlayer(DreamPlayer* p) {
	if (isPlayerPaused(p)) return;

	pauseHandledPhysics(p->mPhysicsElement);
	pauseMugenAnimation(p->mAnimationElement);
	pauseMugenAnimation(p->mShadow.mAnimationElement);
	pauseMugenAnimation(p->mReflection.mAnimationElement);
	pauseDreamRegisteredStateMachine(p->mStateMachineID);
}

static void forceUnpausePlayer(DreamPlayer* p) {
	resumeHandledPhysics(p->mPhysicsElement);
	unpauseMugenAnimation(p->mAnimationElement);
	unpauseMugenAnimation(p->mShadow.mAnimationElement);
	unpauseMugenAnimation(p->mReflection.mAnimationElement);
	unpauseDreamRegisteredStateMachine(p->mStateMachineID);
	p->mIsHitPaused = 0;
}

static void unpausePlayer(DreamPlayer* p) {
	if (isPlayerPaused(p)) return;
	forceUnpausePlayer(p);
}

void setPlayerHitPaused(DreamPlayer* p, int tDuration)
{
	if (isPlayerProjectile(p)) return;
	pausePlayer(p);

	p->mIsHitPaused = 1;
	p->mHitPauseNow = 0;
	p->mHitPauseDuration = tDuration;
}

void setPlayerUnHitPaused(DreamPlayer* p)
{
	p->mIsHitPaused = 0;

	unpausePlayer(p);
}

static void setPlayerDead(DreamPlayer* p) {
	if (gPlayerDefinition.mIsInTrainingMode) return;
	if (!p->mIsAlive) return;

	p->mIsAlive = 0;
	
	if (!p->mNoKOSoundFlag) {
		tryPlayMugenSound(&p->mHeader->mFiles.mSounds, 11, 0);
	}
	const auto activeVelocity = getActiveHitDataVelocityY(p);
	if (activeVelocity >= -6.0) {
		setActiveHitDataVelocityY(p, -6.0);
	}
	else {
		setActiveHitDataVelocityY(p, activeVelocity - 2.0);
	}
	changePlayerStateToSelf(p, 5080);
}

double getPlayerDeathVelAddY(DreamPlayer * p)
{
	const auto activeVelocity = getActiveHitDataVelocityY(p);
	if (activeVelocity >= -6.0) {
		return -6.0 - activeVelocity;
	}
	else {
		return -2.0;
	}
}

void addPlayerDamage(DreamPlayer* p, DreamPlayer* tDamagingPlayer, int tDamage)
{
	p->mLife -= tDamage;
	if (p->mLife <= 0) {
		p->mCheeseWinFlag = isPlayerGuarding(p);
		p->mSuicideWinFlag = (p->mRootID == tDamagingPlayer->mRootID);
		setPlayerDead(p);
		p->mLife = 0;
	}
	p->mLife = min(p->mLife, getPlayerLifeMax(p));

	if (!isPlayerHelper(p) && !isPlayerProjectile(p)) {
		double perc = p->mLife / (double)p->mHeader->mFiles.mConstants.mHeader.mLife;
		setDreamLifeBarPercentage(p, perc);
	}

}

int getPlayerTargetAmount(DreamPlayer* p)
{
	return getPlayerTargetAmountWithID(p, -1);
}

typedef struct {
	int mAmount;
	int mID;

} FindPlayerTargetCaller;

static void findPlayerTargetCB(void* tCaller, void* tData) {
	FindPlayerTargetCaller* caller = (FindPlayerTargetCaller*)tCaller;
	DreamPlayer* target = (DreamPlayer*)tData;
	if (!isPlayer(target)) return;

	if (caller->mID == -1 || caller->mID == target->mTargetID) {
		caller->mAmount++;
	}

	list_map(&target->mHelpers, findPlayerTargetCB, tCaller);
}

int getPlayerTargetAmountWithID(DreamPlayer* p, int tID)
{
	FindPlayerTargetCaller caller;
	caller.mAmount = 0;
	caller.mID = tID;

	DreamPlayer* root = getPlayerOtherPlayer(p);
	findPlayerTargetCB(&caller, root);
	return caller.mAmount;
}

DreamPlayer* getPlayerByIndex(int i) {
	i = min(i, list_size(&gPlayerDefinition.mAllPlayers) - 1);
	DreamPlayer* p = (DreamPlayer*)list_get(&gPlayerDefinition.mAllPlayers, i);
	return p;
}

int getTotalPlayerAmount()
{
	return list_size(&gPlayerDefinition.mAllPlayers);
}

int getPlayerHelperAmount(DreamPlayer* p)
{
	return list_size(&p->mHelpers);
}

typedef struct {
	int mSearchID;
	int mFoundAmount;
} PlayerHelperCountCaller;

static void countHelperByIDCB(void* tCaller, void* tData) {
	PlayerHelperCountCaller* caller = (PlayerHelperCountCaller*)tCaller;
	DreamPlayer* helper = (DreamPlayer*)tData;

	if (helper->mID == caller->mSearchID) {
		caller->mFoundAmount++;
	}

	list_map(&helper->mHelpers, countHelperByIDCB, caller);
}

int getPlayerHelperAmountWithID(DreamPlayer* p, int tID)
{
	PlayerHelperCountCaller caller;
	caller.mFoundAmount = 0;
	caller.mSearchID = tID;
	list_map(&p->mHelpers, countHelperByIDCB, &caller);

	return caller.mFoundAmount;
}

typedef struct {
	DreamPlayer* mFoundHelper;
	int mSearchID;

} PlayerHelperFindCaller;

static void findHelperByIDCB(void* tCaller, void* tData) {
	PlayerHelperFindCaller* caller = (PlayerHelperFindCaller*)tCaller;
	DreamPlayer* helper = (DreamPlayer*)tData;

	if (helper->mID == caller->mSearchID) {
		caller->mFoundHelper = helper;
	}
}

DreamPlayer * getPlayerHelperOrNullIfNonexistant(DreamPlayer * p, int tID)
{
	PlayerHelperFindCaller caller;
	caller.mFoundHelper = NULL;
	caller.mSearchID = tID;
	list_map(&p->mHelpers, findHelperByIDCB, &caller);

	return caller.mFoundHelper;
}

int getPlayerProjectileAmount(DreamPlayer* p)
{
	return int_map_size(&p->mProjectiles);
}

typedef struct {
	int mAmount;
	int mSearchID;
} ProjectileSearchCaller;

static void playerProjectileSearchCB(void* tCaller, void* tData) {
	DreamPlayer* projectile = (DreamPlayer*)tData;
	ProjectileSearchCaller* caller = (ProjectileSearchCaller*)tCaller;

	int id = getProjectileID(projectile);
	if (id == caller->mSearchID) {
		caller->mAmount++;
	}
}

int getPlayerProjectileAmountWithID(DreamPlayer * p, int tID)
{
	DreamPlayer* root = p->mRoot;
	ProjectileSearchCaller caller;
	caller.mAmount = 0;
	caller.mSearchID = tID;
	int_map_map(&root->mProjectiles, playerProjectileSearchCB, &caller);
	return caller.mAmount;
}

int getPlayerProjectileTimeSinceCancel(DreamPlayer * p, int tID)
{
	if (!p->mHasLastContactProjectile || !p->mLastContactProjectileWasCanceled) return -1;
	if (tID > 0 && tID != p->mLastContactProjectileID) return -1;
	return p->mLastContactProjectileTime;
}

int getPlayerProjectileTimeSinceContact(DreamPlayer * p, int tID)
{
	if (!p->mHasLastContactProjectile) return -1;
	if (tID > 0 && tID != p->mLastContactProjectileID) return -1;
	return p->mLastContactProjectileTime;
}

int getPlayerProjectileTimeSinceGuarded(DreamPlayer * p, int tID)
{
	if (!p->mHasLastContactProjectile || !p->mLastContactProjectileWasGuarded) return -1;
	if (tID > 0 && tID != p->mLastContactProjectileID) return -1;
	return p->mLastContactProjectileTime;
}

int getPlayerProjectileTimeSinceHit(DreamPlayer * p, int tID)
{
	if (!p->mHasLastContactProjectile || !p->mLastContactProjectileWasHit) return -1;
	if (tID > 0 && tID != p->mLastContactProjectileID) return -1;
	return p->mLastContactProjectileTime;
}

int getPlayerProjectileHit(DreamPlayer * p, int tID)
{
	return getPlayerProjectileTimeSinceHit(p, tID);
}

int getPlayerProjectileContact(DreamPlayer * p, int tID)
{
	return getPlayerProjectileTimeSinceContact(p, tID);
}

int getPlayerProjectileGuarded(DreamPlayer * p, int tID)
{
	return getPlayerProjectileTimeSinceGuarded(p, tID);
}

int getPlayerTimeLeftInHitPause(DreamPlayer* p)
{
	if (!p->mIsHitPaused) return 0;

	return p->mHitPauseDuration - p->mHitPauseNow;
}

double getPlayerFrontAxisDistanceToScreen(DreamPlayer* p)
{
	double x = getPlayerPositionX(p, getPlayerCoordinateP(p));
	double screenX = getPlayerScreenEdgeInFrontX(p);
	if (getPlayerIsFacingRight(p)) return screenX - x;
	else return x - screenX;
}

double getPlayerBackAxisDistanceToScreen(DreamPlayer* p)
{
	double x = getPlayerPositionX(p, getPlayerCoordinateP(p));
	double screenX = getPlayerScreenEdgeInBackX(p);

	if (getPlayerIsFacingRight(p)) return x - screenX;
	else return screenX - x;
}

double getPlayerFrontBodyDistanceToScreen(DreamPlayer* p)
{
	double x = getPlayerFrontXStage(p, getPlayerCoordinateP(p));
	double screenX = getPlayerScreenEdgeInFrontX(p);

	return fabs(screenX - x);
}

double getPlayerBackBodyDistanceToScreen(DreamPlayer* p)
{
	double x = getPlayerBackXStage(p, getPlayerCoordinateP(p));
	double screenX = getPlayerScreenEdgeInBackX(p);

	return fabs(screenX - x);
}

double getPlayerFrontWidth(DreamPlayer* p) {
	if (p->mHeader->mFiles.mConstants.mSizeData.mHasAttackWidth && getPlayerStateMoveType(p) == MUGEN_STATE_MOVE_TYPE_ATTACK) {
		return p->mHeader->mFiles.mConstants.mSizeData.mAttackWidth.y;
	}
	else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) {
		return p->mHeader->mFiles.mConstants.mSizeData.mAirFrontWidth;
	}
	else {
		return p->mHeader->mFiles.mConstants.mSizeData.mGroundFrontWidth;
	}
}

double getPlayerFrontWidthPlayer(DreamPlayer * p)
{
	if (p->mWidthFlag) {
		return p->mOneTickPlayerWidth.x;
	}
	else {
		return getPlayerFrontWidth(p);
	}
}

double getPlayerFrontWidthStage(DreamPlayer * p)
{
	if (p->mWidthFlag) {
		return p->mOneTickStageWidth.x;
	}
	else {
		return getPlayerFrontWidth(p);
	}
}

double getPlayerBackWidth(DreamPlayer* p) {
	if (p->mHeader->mFiles.mConstants.mSizeData.mHasAttackWidth && getPlayerStateMoveType(p) == MUGEN_STATE_MOVE_TYPE_ATTACK) {
		return p->mHeader->mFiles.mConstants.mSizeData.mAttackWidth.x;
	}
	else if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) {
		return p->mHeader->mFiles.mConstants.mSizeData.mAirBackWidth;
	}
	else {
		return p->mHeader->mFiles.mConstants.mSizeData.mGroundBackWidth;
	}
}

double getPlayerBackWidthPlayer(DreamPlayer * p)
{
	if (p->mWidthFlag) {
		return p->mOneTickPlayerWidth.y;
	}
	else {
		return getPlayerBackWidth(p);
	}
}

double getPlayerBackWidthStage(DreamPlayer * p)
{
	if (p->mWidthFlag) {
		return p->mOneTickStageWidth.y;
	}
	else {
		return getPlayerBackWidth(p);
	}
}


static double getPlayerFrontXGeneral(DreamPlayer* p, int tCoordinateP, double(*tWidthFunction)(DreamPlayer*))
{
	double scale = tCoordinateP / getPlayerCoordinateP(p);
	double x = getPlayerPositionX(p, tCoordinateP);
	if (p->mFaceDirection == FACE_DIRECTION_RIGHT) return x + tWidthFunction(p) * scale;
	else return x - tWidthFunction(p) * scale;
}

double getPlayerFrontX(DreamPlayer* p, int tCoordinateP)
{
	return getPlayerFrontXGeneral(p, tCoordinateP, getPlayerFrontWidth);
}

double getPlayerFrontXPlayer(DreamPlayer * p, int tCoordinateP)
{
	return getPlayerFrontXGeneral(p, tCoordinateP, getPlayerFrontWidthPlayer);
}

double getPlayerFrontXStage(DreamPlayer * p, int tCoordinateP)
{
	return getPlayerFrontXGeneral(p, tCoordinateP, getPlayerFrontWidthStage);
}

static double getPlayerBackXGeneral(DreamPlayer* p, int tCoordinateP, double(*tWidthFunction)(DreamPlayer*)) {
	double scale = tCoordinateP / getPlayerCoordinateP(p);
	double x = getPlayerPositionX(p, tCoordinateP);
	if (p->mFaceDirection == FACE_DIRECTION_RIGHT) return x - tWidthFunction(p) * scale;
	else return x + tWidthFunction(p) * scale;
}

double getPlayerBackX(DreamPlayer* p, int tCoordinateP)
{
	return getPlayerBackXGeneral(p, tCoordinateP, getPlayerBackWidth);
}

double getPlayerBackXPlayer(DreamPlayer * p, int tCoordinateP)
{
	return getPlayerBackXGeneral(p, tCoordinateP, getPlayerBackWidthPlayer);
}

double getPlayerBackXStage(DreamPlayer * p, int tCoordinateP)
{
	return getPlayerBackXGeneral(p, tCoordinateP, getPlayerBackWidthStage);
}

double getPlayerScreenEdgeInFrontX(DreamPlayer* p)
{
	double x = getDreamCameraPositionX(getPlayerCoordinateP(p));

	if (p->mFaceDirection == FACE_DIRECTION_RIGHT) return x + p->mHeader->mConstants.mLocalCoordinates.x;
	else return  x;
}

double getPlayerScreenEdgeInBackX(DreamPlayer* p)
{
	double x = getDreamCameraPositionX(getPlayerCoordinateP(p));

	if (p->mFaceDirection == FACE_DIRECTION_RIGHT) return x;
	else return  x + p->mHeader->mConstants.mLocalCoordinates.x;
}

double getPlayerDistanceToFrontOfOtherPlayerX(DreamPlayer* p)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p);
	double x1 = getPlayerFrontXPlayer(p, getPlayerCoordinateP(p));
	double x2 = getPlayerFrontXPlayer(otherPlayer, getPlayerCoordinateP(p));

	return fabs(x2-x1);
}

static double getPlayerAxisDistanceForTwoReferencesX(DreamPlayer* p1, DreamPlayer* p2) {

	double x1 = getPlayerPositionX(p1, getPlayerCoordinateP(p1));
	double x2 = getPlayerPositionX(p2, getPlayerCoordinateP(p1));

	if(getPlayerIsFacingRight(p1)) return x2 - x1;
	else return x1 - x2;
}

double getPlayerAxisDistanceForTwoReferencesY(DreamPlayer* p1, DreamPlayer* p2)
{
	double y1 = getPlayerPositionY(p1, getPlayerCoordinateP(p1));
	double y2 = getPlayerPositionY(p2, getPlayerCoordinateP(p1));

	return y2 - y1;
}

double getPlayerAxisDistanceX(DreamPlayer* p)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p);
	return getPlayerAxisDistanceForTwoReferencesX(p, otherPlayer);
}

double getPlayerAxisDistanceY(DreamPlayer* p)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p);
	return getPlayerAxisDistanceForTwoReferencesY(p, otherPlayer);
}

double getPlayerDistanceToRootX(DreamPlayer * p)
{
	DreamPlayer* otherPlayer = p->mRoot;
	return getPlayerAxisDistanceForTwoReferencesX(p, otherPlayer);
}

double getPlayerDistanceToRootY(DreamPlayer * p)
{
	DreamPlayer* otherPlayer = p->mRoot;
	return getPlayerAxisDistanceForTwoReferencesY(p, otherPlayer);
}

double getPlayerDistanceToParentX(DreamPlayer * p)
{
	if (!p->mParent) return 0;
	DreamPlayer* otherPlayer = p->mParent;
	return getPlayerAxisDistanceForTwoReferencesX(p, otherPlayer);
}

double getPlayerDistanceToParentY(DreamPlayer * p)
{
	if (!p->mParent) return 0;
	DreamPlayer* otherPlayer = p->mParent;
	return getPlayerAxisDistanceForTwoReferencesY(p, otherPlayer);
}

int getPlayerGroundSizeFront(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mGroundFrontWidth;
}

void setPlayerGroundSizeFront(DreamPlayer * p, int tGroundSizeFront)
{
	p->mHeader->mFiles.mConstants.mSizeData.mGroundFrontWidth = tGroundSizeFront;
}

int getPlayerGroundSizeBack(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mGroundBackWidth;
}

void setPlayerGroundSizeBack(DreamPlayer * p, int tGroundSizeBack)
{
	p->mHeader->mFiles.mConstants.mSizeData.mGroundBackWidth = tGroundSizeBack;
}

int getPlayerAirSizeFront(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mAirFrontWidth;
}

void setPlayerAirSizeFront(DreamPlayer * p, int tAirSizeFront)
{
	p->mHeader->mFiles.mConstants.mSizeData.mAirFrontWidth = tAirSizeFront;
}

int getPlayerAirSizeBack(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mAirBackWidth;
}

void setPlayerAirSizeBack(DreamPlayer * p, int tAirSizeBack)
{
	p->mHeader->mFiles.mConstants.mSizeData.mAirBackWidth = tAirSizeBack;
}

int getPlayerHeight(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mHeight;
}

void setPlayerHeight(DreamPlayer * p, int tHeight)
{
	p->mHeader->mFiles.mConstants.mSizeData.mHeight = tHeight;
}

static void increaseSinglePlayerRoundsExisted(DreamPlayer * p);
static void increasePlayerRoundsExistedCB(void* tCaller, void* tData) {
	(void)tCaller;
	DreamPlayer* p = (DreamPlayer*)tData;
	increaseSinglePlayerRoundsExisted(p);
}

static void increaseSinglePlayerRoundsExisted(DreamPlayer * p)
{
	p->mRoundsExisted++;
	list_map(&p->mHelpers, increasePlayerRoundsExistedCB, NULL);
}

void increasePlayerRoundsExisted()
{
	increaseSinglePlayerRoundsExisted(&gPlayerDefinition.mPlayers[0]);
	increaseSinglePlayerRoundsExisted(&gPlayerDefinition.mPlayers[1]);
}


void increasePlayerRoundsWon(DreamPlayer * p)
{
	p->mRoundsWon++;
}

int hasPlayerWonByKO(DreamPlayer* p)
{
	return isDreamRoundKO() && (getPlayerLife(getPlayerRoot(p)) > getPlayerLife(getPlayerOtherPlayer(p)));
}

int hasPlayerLostByKO(DreamPlayer * p)
{
	return isDreamRoundKO() && (getPlayerLife(getPlayerRoot(p)) < getPlayerLife(getPlayerOtherPlayer(p)));
}

int hasPlayerWonPerfectly(DreamPlayer* p)
{
	return hasPlayerWon(p) && p->mLife == p->mHeader->mFiles.mConstants.mHeader.mLife;
}

int hasPlayerWon(DreamPlayer* p)
{
	return p->mRoundsWon == getRoundsToWin(); 
}

int hasPlayerLost(DreamPlayer* p)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p);
	return hasPlayerWon(otherPlayer); 
}

int hasPlayerDrawn(DreamPlayer * /*p*/)
{
	return isDreamRoundDraw();
}

int hasPlayerMoveHitOtherPlayer(DreamPlayer* p)
{
	DreamMugenStateMoveType type = getPlayerStateMoveType(p);
	int isInAttackState = type == MUGEN_STATE_MOVE_TYPE_ATTACK;
	int isOtherPlayerHit = isPlayerHit(getPlayerOtherPlayer(p));
	int wasItCurrentAttack = !isHitDataActive(p);

	return isInAttackState && isOtherPlayerHit && wasItCurrentAttack;
}

int isPlayerHit(DreamPlayer* p)
{
	DreamMugenStateMoveType moveType = getPlayerStateMoveType(p);
	return moveType == MUGEN_STATE_MOVE_TYPE_BEING_HIT;
}

int hasPlayerMoveBeenReversedByOtherPlayer(DreamPlayer * p)
{
	return p->mHasMoveBeenReversed;
}

int getPlayerMoveHit(DreamPlayer * p)
{
	return p->mMoveHit;
}

void setPlayerMoveHit(DreamPlayer * p)
{
	p->mMoveHit = 1;
}

void setPlayerMoveHitReset(DreamPlayer * p)
{
	p->mMoveContactCounter = 0;
	p->mMoveHit = 0;
	p->mMoveGuarded = 0;
	p->mHasMoveBeenReversed = 0;
}

int getPlayerMoveGuarded(DreamPlayer * p)
{
	return p->mMoveGuarded;
}

void setPlayerMoveGuarded(DreamPlayer * p)
{
	p->mMoveGuarded = 1;
}

int getLastPlayerHitGuarded(DreamPlayer * p)
{
	return p->mLastHitGuarded;
}

int getPlayerFallAmountInCombo(DreamPlayer* p)
{
	return p->mFallAmountInCombo;
}

void increasePlayerFallAmountInCombo(DreamPlayer* p)
{
	p->mFallAmountInCombo++;
}

void resetPlayerFallAmountInCombo(DreamPlayer* p)
{
	p->mFallAmountInCombo = 0;
}

int getPlayerHitCount(DreamPlayer* p)
{
	return p->mHitCount;
}

int getPlayerUniqueHitCount(DreamPlayer * p)
{
	return getPlayerHitCount(p);
}

void increasePlayerHitCount(DreamPlayer* p)
{
	p->mHitCount++;
}

void resetPlayerHitCount(DreamPlayer* p)
{
	p->mHitCount = 0;
	resetPlayerFallAmountInCombo(p);
}

void increasePlayerComboCounter(DreamPlayer * p, int tValue)
{
	increaseComboCounter(p, tValue);
	increaseDisplayedComboCounter(p, tValue);
}

double getPlayerAttackMultiplier(DreamPlayer * p)
{
	return p->mAttackMultiplier;
}

void setPlayerAttackMultiplier(DreamPlayer* p, double tValue)
{
	p->mAttackMultiplier = tValue;
}

double getPlayerFallDefenseMultiplier(DreamPlayer* p)
{
	int f = p->mHeader->mFiles.mConstants.mHeader.mFallDefenseUp;
	return 100.0 / (f+100);
}

void setPlayerHuman(int i)
{
	DreamPlayer* p = getRootPlayer(i);
	p->mAILevel = 0;
}

void setPlayerArtificial(int i, int tValue)
{
	DreamPlayer* p = getRootPlayer(i);
	p->mAILevel = tValue;
}

int isPlayerHuman(DreamPlayer* p) {
	return !getPlayerAILevel(p);
}

int getPlayerAILevel(DreamPlayer* p)
{
	return p->mAILevel;
}

void setPlayerStartLifePercentage(int tIndex, double tPercentage) {
	gPlayerDefinition.mPlayers[tIndex].mStartLifePercentage = tPercentage;
}

double getPlayerLifePercentage(DreamPlayer* p) {
	return p->mLife / (double)p->mHeader->mFiles.mConstants.mHeader.mLife;
}

void setPlayerLife(DreamPlayer * p, DreamPlayer* tLifeGivingPlayer, int tLife)
{
	int delta = tLife - p->mLife;
	addPlayerDamage(p, tLifeGivingPlayer, -delta);
}

void addPlayerLife(DreamPlayer * p, DreamPlayer* tLifeGivingPlayer, int tLife)
{
	addPlayerDamage(p, tLifeGivingPlayer, -tLife);
}

int getPlayerLife(DreamPlayer* p)
{
	return p->mLife;
}

int getPlayerLifeMax(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mLife;
}

int getPlayerPower(DreamPlayer* p)
{
	return p->mPower;
}

int getPlayerPowerMax(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mPower;
}

void setPlayerPower(DreamPlayer * p, int tPower)
{
	p->mPower = max(0, min(getPlayerPowerMax(p), tPower));

	double perc = p->mPower / (double)p->mHeader->mFiles.mConstants.mHeader.mPower;
	setDreamPowerBarPercentage(p, perc, p->mPower);
}

void addPlayerPower(DreamPlayer* p, int tPower)
{
	setPlayerPower(p, p->mPower + tPower);
}

typedef struct {
	int mIsAttacking;

	int mIsInDistance;
	DreamPlayer* p;
} PlayerBeingAttackedSearchCaller;

static void searchPlayerBeingAttackedRecursive(void* tCaller, void* tData) {
	PlayerBeingAttackedSearchCaller* caller = (PlayerBeingAttackedSearchCaller*)tCaller;
	DreamPlayer* p = (DreamPlayer*)tData;
	caller->mIsAttacking |= isHitDataActive(p);

	list_map(&p->mHelpers, searchPlayerBeingAttackedRecursive, caller);
	int_map_map(&p->mProjectiles, searchPlayerBeingAttackedRecursive, caller);
}

int isPlayerBeingAttacked(DreamPlayer* p) {
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p);
	PlayerBeingAttackedSearchCaller caller;
	caller.mIsAttacking = 0;
	searchPlayerBeingAttackedRecursive(&caller, otherPlayer);
	return caller.mIsAttacking;
}

static void searchPlayerInGuardDistanceRecursive(void* tCaller, void* tData) {
	PlayerBeingAttackedSearchCaller* caller = (PlayerBeingAttackedSearchCaller*)tCaller;
	DreamPlayer* otherPlayer = (DreamPlayer*)tData;

	int isAttacking = isHitDataActive(otherPlayer);
	if (isAttacking) {
		caller->mIsAttacking = 1;
		double dist = fabs(getPlayerAxisDistanceForTwoReferencesX(otherPlayer, caller->p));
		caller->mIsInDistance = dist < getHitDataGuardDistance(otherPlayer);
	}

	list_map(&otherPlayer->mHelpers, searchPlayerInGuardDistanceRecursive, caller);
	int_map_map(&otherPlayer->mProjectiles, searchPlayerInGuardDistanceRecursive, caller);
}

int isPlayerInGuardDistance(DreamPlayer* p)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p);

	PlayerBeingAttackedSearchCaller caller;
	caller.mIsAttacking = 0;
	caller.mIsInDistance = 0;
	caller.p = p;

	searchPlayerInGuardDistanceRecursive(&caller, otherPlayer);

	return caller.mIsAttacking && caller.mIsInDistance;
}

int getDefaultPlayerAttackDistance(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mAttackDistance;
}

Position getPlayerHeadPosition(DreamPlayer * p)
{
	return makePosition(getPlayerHeadPositionX(p), getPlayerHeadPositionY(p), 0);
}

double getPlayerHeadPositionX(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mHeadPosition.x;
}

double getPlayerHeadPositionY(DreamPlayer* p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mHeadPosition.y;
}

void setPlayerHeadPosition(DreamPlayer * p, double tX, double tY)
{
	p->mHeader->mFiles.mConstants.mSizeData.mHeadPosition = makePosition(tX, tY, 0);
}

Position getPlayerMiddlePosition(DreamPlayer * p)
{
	return makePosition(getPlayerMiddlePositionX(p), getPlayerMiddlePositionY(p), 0);
}

double getPlayerMiddlePositionX(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mMidPosition.x;
}

double getPlayerMiddlePositionY(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mMidPosition.y;
}

void setPlayerMiddlePosition(DreamPlayer* p, double tX, double tY)
{
	p->mHeader->mFiles.mConstants.mSizeData.mMidPosition = makePosition(tX, tY, 0);
}

int getPlayerShadowOffset(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mShadowOffset;
}

void setPlayerShadowOffset(DreamPlayer * p, int tOffset)
{
	p->mHeader->mFiles.mConstants.mSizeData.mShadowOffset = tOffset;
}

int isPlayerHelper(DreamPlayer* p)
{
	return p->mIsHelper;
}

void setPlayerIsFacingRight(DreamPlayer * p, int tIsFacingRight)
{
	setPlayerFaceDirection(p, tIsFacingRight ? FACE_DIRECTION_RIGHT : FACE_DIRECTION_LEFT);
}

int getPlayerIsFacingRight(DreamPlayer* p)
{
	return p->mFaceDirection == FACE_DIRECTION_RIGHT;
}

void turnPlayerAround(DreamPlayer * p)
{
	setPlayerIsFacingRight(p, !getPlayerIsFacingRight(p));
}

DreamPlayer* getPlayerOtherPlayer(DreamPlayer* p) {
	return p->mOtherPlayer;
}

double getPlayerScaleX(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mScale.x * p->mRelativeScale.x;
}

void setPlayerRelativeScaleX(DreamPlayer * p, double tScaleX)
{
	p->mRelativeScale.x = tScaleX;
}

double getPlayerScaleY(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mSizeData.mScale.y * p->mRelativeScale.y;
}

void setPlayerRelativeScaleY(DreamPlayer * p, double tScaleY)
{
	p->mRelativeScale.y = tScaleY;
}

DreamPlayer * clonePlayerAsHelper(DreamPlayer * p)
{
	int helperIDInStore = stl_int_map_get_id();
	DreamPlayer* helper = &gPlayerDefinition.mHelperStore[helperIDInStore];
	*helper = *p;
	helper->mHelperIDInStore = helperIDInStore;

	resetHelperState(helper);
	setPlayerExternalDependencies(helper);
	loadPlayerShadow(helper);
	loadPlayerReflection(helper);
	loadPlayerDebug(helper);
	setDreamRegisteredStateToHelperMode(helper->mStateMachineID);

	helper->mParent = p;
	helper->mIsHelper = 1;
	helper->mHelperIDInParent = list_push_back(&p->mHelpers, helper);
	helper->mHelperIDInRoot = list_push_back(&gPlayerDefinition.mAllPlayers, helper);

	return helper;
}

typedef struct {
	DreamPlayer* mParent;

} MovePlayerHelperCaller;

static int moveSinglePlayerHelper(void* tCaller, void* tData) {
	MovePlayerHelperCaller* caller = (MovePlayerHelperCaller*)tCaller;
	DreamPlayer* helper = (DreamPlayer*)tData;
	DreamPlayer* parent = caller->mParent;
	DreamPlayer* parentParent = parent->mParent;
	
	helper->mParent = parentParent;
	helper->mHelperIDInParent = list_push_back(&parentParent->mHelpers, helper);

	return 1;
}

static void movePlayerHelpersToParent(DreamPlayer* p) {
	MovePlayerHelperCaller caller;
	caller.mParent = p;
	list_remove_predicate(&p->mHelpers, moveSinglePlayerHelper, &caller);
	delete_list(&p->mHelpers);
}

static void removePlayerBindingCB(void* tCaller, void* tData) {
	(void)tCaller;
	DreamPlayer* p = (DreamPlayer*)tData;
	removePlayerBindingInternal(p);
}

static void removePlayerBoundHelpers(DreamPlayer* p) {
	list_map(&p->mBoundHelpers, removePlayerBindingCB, NULL);
	delete_list(&p->mBoundHelpers);
}

static void destroyGeneralPlayer(DreamPlayer* p) {
	removeAllExplodsForPlayer(p);
	removeMugenAnimation(p->mAnimationElement);
	removeMugenAnimation(p->mShadow.mAnimationElement);
	removeMugenAnimation(p->mReflection.mAnimationElement);
	removeMugenText(p->mDebug.mCollisionTextID);
	removeFromPhysicsHandler(p->mPhysicsElement);
	p->mIsDestroyed = 1;
}

void destroyPlayer(DreamPlayer * p)
{
	if (!p->mIsHelper) {
		logWarningFormat("Warning: trying to destroy player %d %d who is not a helper. Ignoring.\n", p->mRootID, p->mID);
		return;
	}

	assert(p->mIsHelper);
	assert(p->mParent);
	assert(p->mHelperIDInParent != -1);

	logFormat("destroy %d %d\n", p->mRootID, p->mID);

	list_remove(&gPlayerDefinition.mAllPlayers, p->mHelperIDInRoot);
	removePlayerBoundHelpers(p);
	removePlayerBindingInternal(p);
	movePlayerHelpersToParent(p);
	destroyGeneralPlayer(p);
}

int getPlayerID(DreamPlayer * p)
{
	return p->mID;
}

void setPlayerID(DreamPlayer * p, int tID)
{
	logFormat("%d add helper %d\n", p->mRootID, tID);
	p->mID = tID;
}

void setPlayerHelperControl(DreamPlayer * p, int tCanControl)
{
	if (!tCanControl) {
		setDreamRegisteredStateDisableCommandState(p->mStateMachineID);
	}
}

static void addProjectileToRoot(DreamPlayer* tPlayer, DreamPlayer* tProjectile) {
	DreamPlayer* root = tPlayer->mRoot;

	tProjectile->mProjectileID = int_map_push_back(&root->mProjectiles, tProjectile);
}

DreamPlayer * createNewProjectileFromPlayer(DreamPlayer * p)
{
	int helperIDInStore = stl_int_map_get_id();
	DreamPlayer* helper = &gPlayerDefinition.mHelperStore[helperIDInStore];
	*helper = *p;
	helper->mHelperIDInStore = helperIDInStore;

	resetHelperState(helper);
	setPlayerExternalDependencies(helper);
	loadPlayerShadow(helper);
	loadPlayerReflection(helper);
	loadPlayerDebug(helper);
	disableDreamRegisteredStateMachine(helper->mStateMachineID);
	addProjectileToRoot(p, helper);
	addAdditionalProjectileData(helper);
	setPlayerPhysics(helper, MUGEN_STATE_PHYSICS_NONE);
	setPlayerIsFacingRight(helper, getPlayerIsFacingRight(p));
	helper->mParent = p;
	helper->mIsProjectile = 1;

	return helper;
}

static void removeProjectileFromPlayer(DreamPlayer* p) {
	DreamPlayer* root = getPlayerRoot(p);
	int_map_remove(&root->mProjectiles, p->mProjectileID);
}

void removeProjectile(DreamPlayer* p) {
	assert(p->mIsProjectile);
	assert(p->mProjectileID != -1);
	removeAdditionalProjectileData(p);
	removeProjectileFromPlayer(p);
	removeDreamRegisteredStateMachine(p->mStateMachineID);
	unloadHelperStateWithoutFreeingOwnedHelpersAndProjectile(p);
	destroyGeneralPlayer(p);
	updatePlayerDestruction(p);
}

int getPlayerControlTime(DreamPlayer * p)
{
	if (getPlayerStateType(p) == MUGEN_STATE_TYPE_AIR) {
		return getActiveHitDataAirGuardControlTime(p);
	}
	else {
		return getActiveHitDataGuardControlTime(p);
	}
}

int getPlayerRecoverTime(DreamPlayer * p)
{
	if (!p->mIsLyingDown) return 0;
	return p->mHeader->mFiles.mConstants.mHeader.mLiedownTime - p->mLyingDownTime;
}

void setPlayerTempScaleActive(DreamPlayer * p, Vector3D tScale)
{
	p->mTempScale = tScale;
}

void setPlayerDrawAngleActive(DreamPlayer * p)
{
	p->mIsAngleActive = 1;
}

void addPlayerDrawAngle(DreamPlayer * p, double tAngle)
{
	p->mAngle += tAngle;
}

void multiplyPlayerDrawAngle(DreamPlayer * p, double tFactor)
{
	p->mAngle *= tFactor;
}

void setPlayerDrawAngleValue(DreamPlayer * p, double tAngle)
{
	p->mAngle = tAngle;
}

static int isHelperBoundToPlayer(DreamPlayer* tHelper, DreamPlayer* tTestBind) {
	if (!tHelper->mIsBound) return 0;
	return tHelper->mBoundTarget == tTestBind;
}

static void bindHelperToPlayer(DreamPlayer* tHelper, DreamPlayer* tBind, int tTime, int tFacing, Vector3D tOffset, DreamPlayerBindPositionType tType) {
	removePlayerBindingInternal(tHelper);
	tHelper->mIsBound = 1;
	tHelper->mBoundNow = 0;
	tHelper->mBoundDuration = tTime;
	tHelper->mBoundFaceSet = tFacing;
	tHelper->mBoundOffset = tOffset;
	tHelper->mBoundPositionType = tType;
	tHelper->mBoundTarget = tBind;

	if (isHelperBoundToPlayer(tBind, tHelper)) {
		removePlayerBindingInternal(tBind);
	}

	updateBindingPosition(tHelper);

	tHelper->mBoundID = list_push_back(&tBind->mBoundHelpers, tHelper);
}

void bindPlayerToRoot(DreamPlayer * p, int tTime, int tFacing, Vector3D tOffset)
{
	bindHelperToPlayer(p, p->mRoot, tTime, tFacing, tOffset, PLAYER_BIND_POSITION_TYPE_AXIS);
}

void bindPlayerToParent(DreamPlayer * p, int tTime, int tFacing, Vector3D tOffset)
{
	bindHelperToPlayer(p, p->mParent, tTime, tFacing, tOffset, PLAYER_BIND_POSITION_TYPE_AXIS);
}

typedef struct {
	DreamPlayer* mBindRoot;
	int mID;
	Position mOffset;
	int mTime;
	DreamPlayerBindPositionType mBindPositionType; 
} BindPlayerTargetsCaller;

static void bindSinglePlayerToTarget(DreamPlayer* tBindRoot, DreamPlayer* tTarget, BindPlayerTargetsCaller* tCaller);

static void bindPlayerTargetCB(void* tCaller, void* tData) {
	BindPlayerTargetsCaller* caller = (BindPlayerTargetsCaller*)tCaller;
	DreamPlayer* helper = (DreamPlayer*)tData;

	bindSinglePlayerToTarget(caller->mBindRoot, helper, caller);
}

static void bindSinglePlayerToTarget(DreamPlayer* tBindRoot, DreamPlayer* tTarget, BindPlayerTargetsCaller* tCaller) {

	if (tCaller->mID == -1 || tCaller->mID == tTarget->mTargetID) {
		bindHelperToPlayer(tBindRoot, tTarget, tCaller->mTime, 0, tCaller->mOffset, tCaller->mBindPositionType);
	}

	list_map(&tTarget->mHelpers, bindPlayerTargetCB, tCaller);
}

void bindPlayerToTarget(DreamPlayer * p, int tTime, Vector3D tOffset, DreamPlayerBindPositionType tBindPositionType, int tID)
{
	BindPlayerTargetsCaller caller;
	caller.mBindRoot = p;
	caller.mID = tID;
	caller.mOffset = tOffset;
	caller.mTime = tTime + 1;
	caller.mBindPositionType = tBindPositionType;
	bindSinglePlayerToTarget(p, getPlayerOtherPlayer(p)->mRoot, &caller);
}

int isPlayerBound(DreamPlayer * p)
{
	return p->mIsBound;
}

static void bindSingleTargetToPlayer(DreamPlayer* tBindRoot, DreamPlayer* tTarget, BindPlayerTargetsCaller* tCaller);

static void bindTargetToPlayerCB(void* tCaller, void* tData) {
	BindPlayerTargetsCaller* caller = (BindPlayerTargetsCaller*)tCaller;
	DreamPlayer* helper = (DreamPlayer*)tData;

	bindSingleTargetToPlayer(caller->mBindRoot, helper, caller);
}

static void bindSingleTargetToPlayer(DreamPlayer* tBindRoot, DreamPlayer* tTarget, BindPlayerTargetsCaller* tCaller) {
	if (!isPlayer(tTarget)) return;

	if (tCaller->mID == -1 || tCaller->mID == tTarget->mTargetID) {
		bindHelperToPlayer(tTarget, tBindRoot, tCaller->mTime, 0, tCaller->mOffset, tCaller->mBindPositionType);
	}

	list_map(&tTarget->mHelpers, bindTargetToPlayerCB, tCaller);
}

void bindPlayerTargetToPlayer(DreamPlayer * p, int tTime, Vector3D tOffset, int tID)
{
	BindPlayerTargetsCaller caller;
	caller.mBindRoot = p;
	caller.mID = tID;
	caller.mOffset = tOffset;
	caller.mTime = tTime + 1;
	caller.mBindPositionType = PLAYER_BIND_POSITION_TYPE_AXIS;
	bindSingleTargetToPlayer(p, getPlayerOtherPlayer(p)->mRoot, &caller);
}

// TODO: proper targeting (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/376)
void addPlayerTargetLife(DreamPlayer * p, DreamPlayer* tLifeGivingPlayer, int tID, int tLife, int tCanKill, int tIsAbsolute)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p)->mRoot;
	if (tID != -1 && otherPlayer->mTargetID != tID) return;

	if (!tIsAbsolute) {
		const auto attackMultiplier = getPlayerAttackMultiplier(p);
		const auto defenseMultiplier = (1.0 / getPlayerDefenseMultiplier(otherPlayer));
		if(attackMultiplier != 1.0) tLife = int(tLife * attackMultiplier);
		if(defenseMultiplier != 1.0) tLife = int(tLife * defenseMultiplier);
	}

	int playerLife = getPlayerLife(otherPlayer);
	if (!tCanKill && playerLife + tLife <= 0) {
		tLife = -(playerLife - 1);
	}

	addPlayerDamage(otherPlayer, tLifeGivingPlayer, -tLife);
}

// TODO: proper targeting (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/376)
void addPlayerTargetPower(DreamPlayer * p, int tID, int tPower)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p)->mRoot;
	if (tID != -1 && otherPlayer->mTargetID != tID) return;

	addPlayerPower(otherPlayer, tPower);
}

// TODO: proper targetingv(https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/376)
void addPlayerTargetVelocityX(DreamPlayer * p, int tID, double tValue, int tCoordinateP)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p)->mRoot;
	if (tID != -1 && otherPlayer->mTargetID != tID) return;
	
	addPlayerVelocityX(otherPlayer, tValue, tCoordinateP);
}

// TODO: proper targeting (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/376)
void addPlayerTargetVelocityY(DreamPlayer * p, int tID, double tValue, int tCoordinateP)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p)->mRoot;
	if (tID != -1 && otherPlayer->mTargetID != tID) return;

	addPlayerVelocityY(otherPlayer, tValue, tCoordinateP);
}

// TODO: proper targeting (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/376)
void setPlayerTargetVelocityX(DreamPlayer * p, int tID, double tValue, int tCoordinateP)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p)->mRoot;
	if (tID != -1 && otherPlayer->mTargetID != tID) return;

	setPlayerVelocityX(otherPlayer, tValue, tCoordinateP);
}

// TODO: proper targeting (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/376)
void setPlayerTargetVelocityY(DreamPlayer * p, int tID, double tValue, int tCoordinateP)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p)->mRoot;
	if (tID != -1 && otherPlayer->mTargetID != tID) return;

	setPlayerVelocityY(otherPlayer, tValue, tCoordinateP);
}

void setPlayerTargetControl(DreamPlayer * p, int tID, int tControl)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p)->mRoot;
	if (tID != -1 && otherPlayer->mTargetID != tID) return;

	setPlayerControl(otherPlayer, tControl);
}

void setPlayerTargetFacing(DreamPlayer * p, int tID, int tFacing)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p)->mRoot;
	if (tID != -1 && otherPlayer->mTargetID != tID) return;

	if (tFacing == 1) {
		setPlayerIsFacingRight(otherPlayer, getPlayerIsFacingRight(p));
	}
	else {
		setPlayerIsFacingRight(otherPlayer, !getPlayerIsFacingRight(p));
	}
}

void changePlayerTargetState(DreamPlayer * p, int tID, int tNewState)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p)->mRoot;
	if (tID != -1 && otherPlayer->mTargetID != tID) return;
	changePlayerStateToOtherPlayerStateMachine(otherPlayer, p, tNewState);
}

typedef struct {
	int mSearchID;
	DreamPlayer* mPlayer;

} SearchPlayerIDCaller;

static DreamPlayer* searchSinglePlayerForID(DreamPlayer* p, int tID);

static void searchPlayerForIDCB(void* tCaller, void* tData) {
	SearchPlayerIDCaller* caller = (SearchPlayerIDCaller*)tCaller;
	DreamPlayer* p = (DreamPlayer*)tData;

	DreamPlayer* ret = searchSinglePlayerForID(p, caller->mSearchID);
	if (ret != NULL) {
		caller->mPlayer = ret;
	}
}

static DreamPlayer* searchSinglePlayerForID(DreamPlayer* p, int tID) {
	if (getPlayerID(p) == tID) return p;

	SearchPlayerIDCaller caller;
	caller.mSearchID = tID;
	caller.mPlayer = NULL;
	list_map(&p->mHelpers, searchPlayerForIDCB, &caller);

	return caller.mPlayer;
}

int doesPlayerIDExist(DreamPlayer * p, int tID)
{
	return searchSinglePlayerForID(p->mRoot, tID) != NULL;
}

DreamPlayer * getPlayerByIDOrNullIfNonexistant(DreamPlayer * p, int tID)
{
	return searchSinglePlayerForID(p->mRoot, tID);
}

int getPlayerRoundsExisted(DreamPlayer * p)
{
	return p->mRoundsExisted;
}

int getPlayerPaletteNumber(DreamPlayer * p)
{
	return p->mPreferredPalette;
}

void setPlayerScreenBoundForTick(DreamPlayer * p, int tIsBoundToScreen, int tIsCameraFollowingX, int tIsCameraFollowingY)
{
	p->mIsBoundToScreenForTick = tIsBoundToScreen;
	(void)tIsCameraFollowingX;
	(void)tIsCameraFollowingY;
	// TODO: use parameters (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/304)
}

void setPlayerScreenBoundForever(DreamPlayer * p, int tIsBoundToScreen)
{
	p->mIsBoundToScreenForever = tIsBoundToScreen;
}

static void resetPlayerHitBySlotGeneral(DreamPlayer * p, int tSlot) {
	p->mNotHitBy[tSlot].mFlag2Amount = 0;
	p->mNotHitBy[tSlot].mNow = 0;
	p->mNotHitBy[tSlot].mIsActive = 1;
}

void resetPlayerHitBy(DreamPlayer * p, int tSlot)
{
	resetPlayerHitBySlotGeneral(p, tSlot);
	p->mNotHitBy[tSlot].mIsHitBy = 1;
}

void resetPlayerNotHitBy(DreamPlayer * p, int tSlot)
{
	resetPlayerHitBySlotGeneral(p, tSlot);
	p->mNotHitBy[tSlot].mIsHitBy = 0;
}

void setPlayerNotHitByFlag1(DreamPlayer * p, int tSlot, char * tFlag)
{
	strcpy(p->mNotHitBy[tSlot].mFlag1, tFlag);
	turnStringLowercase(p->mNotHitBy[tSlot].mFlag1);
}

static string copyOverCleanFlag2(char* tSrc) {
	int n = strlen(tSrc);

	string ret;
	int i;
	for (i = 0; i < n; i++) {
		if (tSrc[i] == ' ') continue;
		ret.push_back(tSrc[i]);
	}
	return ret;
}

void addPlayerNotHitByFlag2(DreamPlayer * p, int tSlot, char * tFlag)
{

	if (p->mNotHitBy[tSlot].mFlag2Amount >= MAXIMUM_HITSLOT_FLAG_2_AMOUNT) {
		logWarningFormat("Too many nothitby flags. Ignoring flag %s.", tFlag);
		return;
	}

	string nFlag = copyOverCleanFlag2(tFlag);
	if (nFlag.size() != 2) {
		logErrorFormat("Unable to parse nothitby flag %s. Ignoring.", tFlag);
		return;
	}

	turnStringLowercase(nFlag);

	strcpy(p->mNotHitBy[tSlot].mFlag2[p->mNotHitBy[tSlot].mFlag2Amount], nFlag.data());
	p->mNotHitBy[tSlot].mFlag2Amount++;
}

void setPlayerNotHitByTime(DreamPlayer * p, int tSlot, int tTime)
{
	p->mNotHitBy[tSlot].mTime = tTime;
}

int getDefaultPlayerSparkNumberIsInPlayerFile(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mIsSparkNoInPlayerFile;
}

int getDefaultPlayerSparkNumber(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mSparkNo;
}

int getDefaultPlayerGuardSparkNumberIsInPlayerFile(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mIsGuardSparkNoInPlayerFile;
}

int getDefaultPlayerGuardSparkNumber(DreamPlayer * p)
{
	return p->mHeader->mFiles.mConstants.mHeader.mGuardSparkNo;
}

int isPlayerProjectile(DreamPlayer * p)
{
	return p->mIsProjectile;
}

int isPlayerHomeTeam(DreamPlayer * p)
{
	return p->mRootID;
}

void setPlayerDrawOffsetX(DreamPlayer* p, double tValue, int tCoordinateP) {
	tValue = transformDreamCoordinates(tValue, tCoordinateP, getPlayerCoordinateP(p));
	Position pos = getMugenAnimationPosition(p->mAnimationElement);
	Position basePos = getDreamStageCoordinateSystemOffset(getPlayerCoordinateP(p));
	Position newPos = vecAdd(basePos, makePosition(p->mHeader->mFiles.mConstants.mSizeData.mDrawOffset.x, p->mHeader->mFiles.mConstants.mSizeData.mDrawOffset.y, 0));
	newPos.x += tValue;
	newPos.y = pos.y;
	newPos.z = pos.z;
	p->mDrawOffset.x = tValue;

	setMugenAnimationPosition(p->mAnimationElement, newPos);
}

void setPlayerDrawOffsetY(DreamPlayer* p, double tValue, int tCoordinateP) {
	tValue = transformDreamCoordinates(tValue, tCoordinateP, getPlayerCoordinateP(p));
	Position pos = getMugenAnimationPosition(p->mAnimationElement);
	Position basePos = getDreamStageCoordinateSystemOffset(getPlayerCoordinateP(p));
	Position newPos = vecAdd(basePos, makePosition(p->mHeader->mFiles.mConstants.mSizeData.mDrawOffset.x, p->mHeader->mFiles.mConstants.mSizeData.mDrawOffset.y, 0));
	newPos.x = pos.x;
	newPos.y += tValue;
	newPos.z = pos.z;
	p->mDrawOffset.y = tValue;

	setMugenAnimationPosition(p->mAnimationElement, newPos);
}

void setPlayerOneFrameTransparency(DreamPlayer * p, BlendType tType, int tAlphaSource, int tAlphaDest)
{
	setMugenAnimationBlendType(p->mAnimationElement, tType);
	setMugenAnimationTransparency(p->mAnimationElement, tAlphaSource / 256.0);
	(void)tAlphaDest; // TODO: use (https://dev.azure.com/captdc/DogmaRnDA/_workitems/edit/375)
	p->mTransparencyFlag = 1;
}

void setPlayerWidthOneFrame(DreamPlayer * p, Vector3DI tEdgeWidth, Vector3DI tPlayerWidth)
{
	p->mOneTickStageWidth = tEdgeWidth;
	p->mOneTickPlayerWidth = tPlayerWidth;

	p->mWidthFlag = 1;
}

void addPlayerDust(DreamPlayer * p, int tDustIndex, Position tPos, int tSpacing)
{
	int time = getDreamGameTime();
	int since = time - p->mDustClouds[tDustIndex].mLastDustTime;
	if (since < tSpacing) return;

	p->mDustClouds[tDustIndex].mLastDustTime = time;
	Position playerPosition = getPlayerPosition(p, getPlayerCoordinateP(p));
	Position pos = vecAdd(tPos, playerPosition); 
	pos.z = DUST_Z;
	addDreamDustCloud(pos, getPlayerIsFacingRight(p), getPlayerCoordinateP(p));
}

VictoryType getPlayerVictoryType(DreamPlayer * p)
{
	DreamPlayer* otherPlayer = getPlayerOtherPlayer(p);

	if (isTimerFinished()) {
		return VICTORY_TYPE_TIMEOVER;
	}
	else if (otherPlayer->mSuicideWinFlag) {
		return VICTORY_TYPE_SUICIDE;
	}
	else if (getActiveHitDataAttackType(otherPlayer) == MUGEN_ATTACK_TYPE_THROW) {
		return VICTORY_TYPE_THROW;
	}
	else if (otherPlayer->mCheeseWinFlag) {
		return VICTORY_TYPE_CHEESE;
	}
	else if (getActiveHitDataAttackClass(otherPlayer) == MUGEN_ATTACK_CLASS_HYPER) {
		return VICTORY_TYPE_HYPER;
	}
	else if (getActiveHitDataAttackClass(otherPlayer) == MUGEN_ATTACK_CLASS_SPECIAL) {
		return VICTORY_TYPE_SPECIAL;
	}
	else {
		return VICTORY_TYPE_NORMAL;
	}
}

int isPlayerAtFullLife(DreamPlayer * p)
{
	return getPlayerLife(p) == getPlayerLifeMax(p);
}

void setPlayersToTrainingMode()
{
	gPlayerDefinition.mIsInTrainingMode = 1;
}

void setPlayersToRealFightMode()
{
	gPlayerDefinition.mIsInTrainingMode = 0;
}

int isPlayer(DreamPlayer * p)
{
	return list_contains(&gPlayerDefinition.mAllPlayers, p);
}

int isGeneralPlayer(DreamPlayer * p)
{
	if (!p) return 0;
	if ((isPlayerHelper(p) || isPlayerProjectile(p)) && !stl_map_contains(gPlayerDefinition.mHelperStore, p->mHelperIDInStore)) return 0;
	return !isPlayerDestroyed(p);
}

int isPlayerTargetValid(DreamPlayer* p) {
	return p && !isPlayerDestroyed(p);
}

int isPlayerCollisionDebugActive() {
	return gPlayerDefinition.mIsCollisionDebugActive;
}

static void setPlayerCollisionDebugRecursiveCB(void* tCaller, void* tData) {
	(void)tCaller;
	DreamPlayer* p = (DreamPlayer*)tData;
	setMugenAnimationCollisionDebug(p->mAnimationElement, gPlayerDefinition.mIsCollisionDebugActive);

	if (!gPlayerDefinition.mIsCollisionDebugActive) {
		char text[3];
		text[0] = '\0';
		changeMugenText(p->mDebug.mCollisionTextID, text);
	}
}

void setPlayerCollisionDebug(int tIsActive) {
	gPlayerDefinition.mIsCollisionDebugActive = tIsActive;
	list_map(&gPlayerDefinition.mAllPlayers, setPlayerCollisionDebugRecursiveCB, NULL);
}

void turnPlayerTowardsOtherPlayer(DreamPlayer* p) {
	DreamPlayer* p2 = getPlayerOtherPlayer(p);

	double x1 = getHandledPhysicsPositionReference(p->mPhysicsElement)->x;
	double x2 = getHandledPhysicsPositionReference(p2->mPhysicsElement)->x;

	if (x1 > x2) setPlayerFaceDirection(p, FACE_DIRECTION_LEFT);
	else if (x1 < x2) setPlayerFaceDirection(p, FACE_DIRECTION_RIGHT);
}

int isPlayerInputAllowed(DreamPlayer * p)
{
	return (getGameMode() != GAME_MODE_OSU) || isOsuPlayerCommandInputAllowed(p->mRootID);
}

typedef struct {
	double mSpeed;
} SetPlayerSpeedCaller;

static void setSinglePlayerSpeed(void* tCaller, void* tData) {
	SetPlayerSpeedCaller* caller = (SetPlayerSpeedCaller*)tCaller;
	DreamPlayer* player = (DreamPlayer*)tData;
	setMugenAnimationSpeed(player->mAnimationElement, caller->mSpeed);
	setMugenAnimationSpeed(player->mShadow.mAnimationElement, caller->mSpeed);
	setMugenAnimationSpeed(player->mReflection.mAnimationElement, caller->mSpeed);
	setHandledPhysicsSpeed(player->mPhysicsElement, caller->mSpeed);
}

void setPlayersSpeed(double tSpeed)
{
	SetPlayerSpeedCaller caller;
	caller.mSpeed = tSpeed;
	list_map(&gPlayerDefinition.mAllPlayers, setSinglePlayerSpeed, &caller);
	setStateMachineHandlerSpeed(tSpeed);
	gPlayerDefinition.mTimeDilatation = tSpeed;
}
