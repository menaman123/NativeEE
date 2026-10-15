#pragma once
// TitlePlayMain: the play composition's entry, DEFINED by the linked title module
// (games/<serial>_<slug>/, target ps2r_title_play) and called by app/host/main.cpp
// once the title-neutral --version/--help scan is done. The engine only declares it,
// exactly as it declares ActiveTitleProfile() (title_profile.h): the composition
// (which engine mechanisms to arm, which hardware fallbacks, which HLE) is title
// data, so it lives with the title. A build whose title defines no ps2r_title_play
// never compiles a call to this (main.cpp gates on PS2R_TITLE_HAS_PLAY).
int TitlePlayMain(int argc, char** argv);
