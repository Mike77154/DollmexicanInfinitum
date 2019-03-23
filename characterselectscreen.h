#pragma once

#include <prism/wrapper.h>
#include <prism/mugendefreader.h>

Screen* getCharacterSelectScreen();

void setCharacterSelectScreenModeName(char* tModeName);
void setCharacterSelectFinishedCB(void(*tCB)());
void setCharacterSelectStageActive();
void setCharacterSelectStageInactive();
void setCharacterSelectOnePlayer();
void setCharacterSelectOnePlayerSelectAll();
void setCharacterSelectTwoPlayers();
void setCharacterSelectCredits();
void setCharacterSelectStory();

void parseOptionalCharacterSelectParameters(MugenStringVector tVector, int* oOrder, int* oDoesIncludeStage, char* oMusicPath);
void getCharacterSelectNamePath(const char* tName, char* oDst);

void setCharacterRandom(MugenDefScript* tScript, int i);
void setStageRandom(MugenDefScript* tScript);