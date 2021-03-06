
Fastlane installation (execute in parent folder):

    bundle install --binstubs --path vendor/bundle

Then you can use to push apk+metadata directly (requires finished gradle step):

    bin/fastlane supply --track beta # for beta
    bin/fastlane supply              # for releases

There are some useful options/environment variables:

    --skip_upload_images or SUPPLY_SKIP_UPLOAD_IMAGES
    --skip_upload_screenshots or SUPPLY_SKIP_UPLOAD_SCREENSHOTS

NOTE: Connecting to the Google Play Api needs a json key (can be generated by the
Play Console). You can either use the `Appfile` to provide it or use --json_key
(or set SUPPLY_JSON_KEY=/path/to/file.json).

---

Maybe in the future we can even use the lanes described in the Fastfile to invoke
gradle and supply:

    bin/fastlane beta      # for beta
    bin/fastlane playstore # for releases
