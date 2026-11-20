#!/bin/bash
# Builds the TFE Quest APK from the CMake outputs (no Gradle).
set -e
# ROOT is the quest/ folder of this fork. SDL2 is expected in $ROOT/SDL2 and the CMake build in $ROOT/build-android.
ROOT=${ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
SDK=${ANDROID_HOME:-$ROOT/tools/sdk}
A=$ROOT/android
OUT=$ROOT/out
BT=$SDK/build-tools/34.0.0
JAR=$SDK/platforms/android-34/android.jar
NDK=${ANDROID_NDK_HOME:-$ROOT/tools/android-ndk-r27c}
STRIP=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip
TFE=${TFE:-$ROOT/../TheForceEngine}
rm -rf $OUT/stage && mkdir -p $OUT/stage/{classes,dex,apk/lib/arm64-v8a,apk/assets,res}
# Java
find $ROOT/SDL2/android-project/app/src/main/java -name '*.java' > $OUT/stage/java.txt
javac -nowarn --release 11 -cp $JAR -d $OUT/stage/classes @$OUT/stage/java.txt 2>&1 | grep -v 'warning' || true
$SDK/build-tools/35.0.0/d8 --release --min-api 29 --lib $JAR --output $OUT/stage/dex $(find $OUT/stage/classes -name '*.class')
# Resources + manifest
$BT/aapt2 compile --dir $A/res -o $OUT/stage/res/res.zip
$BT/aapt2 link -o $OUT/stage/base.apk -I $JAR --manifest ${MANIFEST:-$A/AndroidManifest.xml} $OUT/stage/res/res.zip
# Native libs
for so in $ROOT/build-android/libmain.so $ROOT/build-android/sdl2/libSDL2.so $EXTRA_SO; do
  $STRIP --strip-unneeded -o $OUT/stage/apk/lib/arm64-v8a/$(basename $so) $so
done
# Assets: TFE support data + manifest
cd $TFE
for d in Captions Documentation ExternalData Fonts Mods Shaders SoundFonts Translation UI_Images UI_Text Voxels; do
  [ -d $d ] && cp -r $d $OUT/stage/apk/assets/
done
cp $A/assets_extra/* $OUT/stage/apk/assets/
# Optional: weapons converted from the player's copy of the DFmod (tools/dfmod/convert.py); not in the source tree.
[ -n "$DFMOD_BIN" ] && cp "$DFMOD_BIN" $OUT/stage/apk/assets/dfmod_weapons.bin
# Optional: DFmod weapons completed with the mod's third-person models (tools/dfmod/combine.py + pack_comb.py), style 4.
[ -n "$DFMOD2_BIN" ] && cp "$DFMOD2_BIN" $OUT/stage/apk/assets/dfmod_combined.bin
(cd $OUT/stage/apk/assets && find . -type f ! -name tfe_manifest.txt ! -name tfe_assets_version.txt ! -name settings_quest_default.ini | sed 's#^\./##' | sort > tfe_manifest.txt)
date +%s > $OUT/stage/apk/assets/tfe_assets_version.txt
cp $OUT/stage/dex/classes.dex $OUT/stage/apk/
cp $OUT/stage/base.apk $OUT/stage/unsigned.apk
(cd $OUT/stage/apk && zip -q -r -0 ../unsigned.apk lib && zip -q -r ../unsigned.apk classes.dex assets)
$BT/zipalign -f -p 4 $OUT/stage/unsigned.apk $OUT/stage/aligned.apk
KS=$A/debug.keystore
[ -f $KS ] || keytool -genkeypair -keystore $KS -storepass android -keypass android -alias tfevr -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=TFE VR,O=vcmatias" >/dev/null 2>&1
$BT/apksigner sign --ks $KS --ks-pass pass:android --key-pass pass:android --out $OUT/${APKNAME:-TFE-VR-Quest.apk} $OUT/stage/aligned.apk
ls -la $OUT/${APKNAME:-TFE-VR-Quest.apk}
