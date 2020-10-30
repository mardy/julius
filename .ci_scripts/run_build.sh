#!/usr/bin/env bash

set -e

case "$BUILD_TARGET" in
"vita")
	docker exec vitasdk /bin/bash -c "cd build && make"
	;;
"switch")
	docker exec switchdev /bin/bash -c "cd build && make"
	;;
"mac")
	cd build && make && make test && make install
	echo "Creating disk image"
	hdiutil create -volname Julius -srcfolder julius.app -ov -format UDZO julius.dmg
	;;
"appimage")
	cd build && make && make test
	make DESTDIR=AppDir install
	cd ..
	./.ci_scripts/package_appimage.sh
	;;
"linux")
	cd build && make && make test
	zip julius.zip julius
	;;
"android")
	cd android
	if [ -f julius.keystore ]
	then
		COMMAND=assembleRelease
		if [ "$TRAVIS_BRANCH" == "master" ]
		then
			COMMAND=publishRelease
		fi
	else
		COMMAND=assembleDebug
	fi
	TERM=dumb ./gradlew $COMMAND
	if [ -f julius/build/outputs/apk/release/julius-release.apk ]
	then
		cp julius/build/outputs/apk/release/julius-release.apk ../build/julius.apk
	fi
	;;
*)
	cd build && make && make test
	;;
esac
