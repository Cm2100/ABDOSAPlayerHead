# ABDOSA Player Head

F4SE/CommonLibF4 plugin that rebuilds **only the player** with custom female front/rear head meshes, while immediately restoring the vanilla head-part model paths so NPCs keep their normal head meshes.

## Required mesh paths

Place the two custom NIFs here:

```text
Data\Meshes\Actors\Character\CharacterAssets\ABDOSAPlayerHead\BaseFemaleHead_faceBones.nif
Data\Meshes\Actors\Character\CharacterAssets\ABDOSAPlayerHead\FemaleheadRear_faceBones.nif
```

The plugin uses the vanilla female head-part records only as a temporary model-path source during the player's head rebuild. It does **not** require a custom HDPT/FacePart ESP and does not edit body or hand meshes.

## Plugin

```text
Data\F4SE\Plugins\ABDOSAPlayerHead.dll
```

The redirect is applied when game data becomes ready and again after loading a save.

## Current test target

Fallout 4 next-gen runtime with current F4SE/CommonLibF4. This branch is intentionally a focused test build for the player's custom head meshes.
