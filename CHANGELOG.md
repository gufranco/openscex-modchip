# [0.19.0](https://github.com/gufranco/openscex-modchip/compare/v0.18.1...v0.19.0) (2026-10-17)


### Bug Fixes

* **bench:** check an EEPROM seed by reading back ([b273bd7](https://github.com/gufranco/openscex-modchip/commit/b273bd70941f73cbf20f58c1b906273c55509bc7))
* **bench:** decide git sources by SHA-256 ([1d1618d](https://github.com/gufranco/openscex-modchip/commit/1d1618dff03b46830a00c9202790bbe7ad2703d1))
* **bench:** excuse sourceless code only in runtime ([64d20b6](https://github.com/gufranco/openscex-modchip/commit/64d20b63cc8f853e0a4d581fb17036254e98bfaa))
* **bench:** fail closed on runner input and halts ([8dcf62e](https://github.com/gufranco/openscex-modchip/commit/8dcf62e5d5d753918a9c1ab3f7fc8bf863e6ef95))
* **bench:** fail on unjudged and partial runs ([54b1a67](https://github.com/gufranco/openscex-modchip/commit/54b1a67d60be2e2a9b81ee6331987fcf5a82e672))
* **bench:** keep the exclusion table off ours ([fef580d](https://github.com/gufranco/openscex-modchip/commit/fef580dd0784828d5467bfd0423aca543c0b9078))
* **bench:** refuse unknown and overlapping records ([bfcb52e](https://github.com/gufranco/openscex-modchip/commit/bfcb52ee133be3da581f01911c2449c277134396))
* **bench:** skip local labels with a trailing byte ([130dd35](https://github.com/gufranco/openscex-modchip/commit/130dd351404988a1458d43a6d3b9d57a533d9228))
* bound each carrier bit cell in time ([3f66a0c](https://github.com/gufranco/openscex-modchip/commit/3f66a0c1217c00a046bf329944336a310a626684))
* record a late carrier before its first string ([59ecf7c](https://github.com/gufranco/openscex-modchip/commit/59ecf7c725174c433928556afb68ea29de256377))
* report a noisy trace line in the diagnosis ([000639b](https://github.com/gufranco/openscex-modchip/commit/000639bd80717551a88f484b2805ba8c085d7b03))
* return from main as MISRA rule 17.4 asks ([b1a80e1](https://github.com/gufranco/openscex-modchip/commit/b1a80e1b7b1e956954a8312f5bdde3727ee74ef7))
* time the frame budget from the first edge ([22a88ad](https://github.com/gufranco/openscex-modchip/commit/22a88ad5f328fa4c418334221c0cbe30978f5fe2))
* **trace:** idle the serial line high from reset ([2bfd6be](https://github.com/gufranco/openscex-modchip/commit/2bfd6beb0a598e8250af645b9aee0de9a9964c34))
* **trace:** show how far a missed lead-in got ([6a96cbb](https://github.com/gufranco/openscex-modchip/commit/6a96cbb52cdf8b2358167f10482aea9c449a9dfd))


### Features

* one symmetric board with optional headers ([bcdfbaf](https://github.com/gufranco/openscex-modchip/commit/bcdfbaf417fb4d61b2c9f8f828d4003207654d2b))

## [0.18.1](https://github.com/gufranco/openscex-modchip/compare/v0.18.0...v0.18.1) (2026-10-15)


### Bug Fixes

* **bench:** fail closed on bad runner input ([d28e711](https://github.com/gufranco/openscex-modchip/commit/d28e711cc784072be6187fbb9945d5d94f19fdc0))
* **bench:** refuse a trace level not 0 or 1 ([6c9b616](https://github.com/gufranco/openscex-modchip/commit/6c9b616dfe5f5e3caded25a2cd19a881690bbd5d))
* **bench:** refuse an unknown path action ([1feaddf](https://github.com/gufranco/openscex-modchip/commit/1feaddf776ced1b2494c49f1177980b69395c07a))
* **bench:** stamp the recipe, refuse no line table ([3601131](https://github.com/gufranco/openscex-modchip/commit/360113152cfa89af1d5a77bc37724207a267ea4c))
* bound docker calls and report a failed build ([7772c8f](https://github.com/gufranco/openscex-modchip/commit/7772c8fbfa4b35ca7897cda65da333f2e72a8e91))
* fail a SUBQ capture that outlasts its frame ([12a7fed](https://github.com/gufranco/openscex-modchip/commit/12a7fed20ecc47a1e8ac3aeba99f502bcdfad78e))
* pause the confirm wait while supply holds ([368ed27](https://github.com/gufranco/openscex-modchip/commit/368ed27bdd6b180f3f5ad65a58023e8c394f6a92))
* read a noisy trace line as unreadable ([4da8c57](https://github.com/gufranco/openscex-modchip/commit/4da8c57b5ae22fb5e2ca25eadb5415f5f272e291))
* record a carrier found after boot ([c8e3518](https://github.com/gufranco/openscex-modchip/commit/c8e3518060e6bc1c65e6bad72a4a0fb9f909d5ce))
* skip an EEPROM read while a write is stuck ([c56fa2c](https://github.com/gufranco/openscex-modchip/commit/c56fa2c615a546f417d49d154e56366fd76dc6b7))

# [0.18.0](https://github.com/gufranco/openscex-modchip/compare/v0.17.0...v0.18.0) (2026-10-14)


### Bug Fixes

* **bench:** bound every tool the bench calls ([164f876](https://github.com/gufranco/openscex-modchip/commit/164f876ef565b04f89d81eaac0d4f83d97e9ef90))
* **bench:** check the shape of committed JSON ([047cea5](https://github.com/gufranco/openscex-modchip/commit/047cea53269bc51cde584a08c2467ae4a32404f4))
* **bench:** decode each burst in its own mode ([04b9bab](https://github.com/gufranco/openscex-modchip/commit/04b9bab16481dda501da23006894df93063c6128))
* **bench:** fail a run whose AVR image crashes ([cb5e6e0](https://github.com/gufranco/openscex-modchip/commit/cb5e6e05686ff397155c168d5e6407a2b8f491fd))
* **bench:** fail when ours could not run ([3a9ea60](https://github.com/gufranco/openscex-modchip/commit/3a9ea6090fb794e06cf942e8bc3253c3005204e6))
* **bench:** judge the cells of garbled strings ([35e1200](https://github.com/gufranco/openscex-modchip/commit/35e12004b263be429bff6ef434387a33550834eb))
* **bench:** read every event of a long timeline ([e1cc787](https://github.com/gufranco/openscex-modchip/commit/e1cc7875c8e01e2ce571f50b36faf9155741257e))
* **bench:** read Liss V1.4 from its original copy ([129e882](https://github.com/gufranco/openscex-modchip/commit/129e8822e1cd8c2d8ecd119849ea76d79c802029))
* **bench:** rearm only on a valid second string ([a99812e](https://github.com/gufranco/openscex-modchip/commit/a99812e7f86014b65f43d918d576dc24737146b7))
* **bench:** rebuild a source image from a new tree ([a62d36b](https://github.com/gufranco/openscex-modchip/commit/a62d36bf3f52ed9217ad99bf5826fc1b12a023ce))
* **bench:** refuse a trace with no end line ([9997626](https://github.com/gufranco/openscex-modchip/commit/9997626b25f15f90aa6c6c219473898f93e95ac6))
* **bench:** use the bytes the manifest verified ([c3c9e3e](https://github.com/gufranco/openscex-modchip/commit/c3c9e3e33869f3515ccfaef5d097e06cbfb2362a))
* **bench:** validate the artifact manifest on load ([95286b8](https://github.com/gufranco/openscex-modchip/commit/95286b8187cf9a4748791ac3e86806e58f6956ca))
* guard the frame capture against a zero count ([633a3de](https://github.com/gufranco/openscex-modchip/commit/633a3de591a4cf5c918e7c29127601e387794418))
* keep the trace line idle after a string ([c87c91b](https://github.com/gufranco/openscex-modchip/commit/c87c91b902f11992db284b6195fd83b7aff55925))
* measure the WFCK sample, keep the 360 us gap ([04f5e8f](https://github.com/gufranco/openscex-modchip/commit/04f5e8f7a62f47484d2271691b931e2a06a67df8))
* start every EEPROM write in erase-and-write ([ac755de](https://github.com/gufranco/openscex-modchip/commit/ac755de3b390cbfffb95803c7c33c458ee4a17f9))
* store learned values only outside the window ([3672fe5](https://github.com/gufranco/openscex-modchip/commit/3672fe58a3880507f142a10dc508ce420b93657f))


### Features

* add a serial SUBQ trace profile ([187c2a6](https://github.com/gufranco/openscex-modchip/commit/187c2a698720e0c599f430e11115ffc919908999))
* **bench:** judge strings after play resumes ([6c90d0e](https://github.com/gufranco/openscex-modchip/commit/6c90d0e20ab7ce1fd2e5e2ccc526629da1432b14))
* explain each decision on the serial trace ([fdf17ce](https://github.com/gufranco/openscex-modchip/commit/fdf17ce96789789402eb54fa6205c63da0b9dcaf))
* **hardware:** add a serial trace header ([d53ce93](https://github.com/gufranco/openscex-modchip/commit/d53ce930b79ee7ccdf9abfba47b6cf3899b7f080))

# [0.17.0](https://github.com/gufranco/openscex-modchip/compare/v0.16.0...v0.17.0) (2026-10-11)


### Bug Fixes

* hold the stealth cap through a counter dip ([85e1ec6](https://github.com/gufranco/openscex-modchip/commit/85e1ec6a4b4c7544760a046a43670a78fb2545e5))


### Features

* harden SUBQ input and EEPROM stores ([dd81849](https://github.com/gufranco/openscex-modchip/commit/dd8184940026dc3b04e3f2c813af9c1725a421fc))

# [0.16.0](https://github.com/gufranco/openscex-modchip/compare/v0.15.0...v0.16.0) (2026-10-11)


### Features

* **hardware:** name datasheet-checked parts ([73e6623](https://github.com/gufranco/openscex-modchip/commit/73e662396141e6232e91e2c799fc98f9031444bb))

# [0.15.0](https://github.com/gufranco/openscex-modchip/compare/v0.14.0...v0.15.0) (2026-10-11)


### Features

* **hardware:** add a RESET pull-up to every board ([9ac3207](https://github.com/gufranco/openscex-modchip/commit/9ac320775b2a286daca930347c85334b17dfb92a))

# [0.14.0](https://github.com/gufranco/openscex-modchip/compare/v0.13.0...v0.14.0) (2026-10-10)


### Features

* **hardware:** draw teardrops on every pad joint ([3d56fc1](https://github.com/gufranco/openscex-modchip/commit/3d56fc1af3350b0025e692e6f82854366f1ff87b))

# [0.13.0](https://github.com/gufranco/openscex-modchip/compare/v0.12.0...v0.13.0) (2026-10-10)


### Features

* adaptive timing and safer disc swaps ([1ad41ed](https://github.com/gufranco/openscex-modchip/commit/1ad41ed470f801c2550269c1e67b38cc6bc5a222))
* **hardware:** add carrier, mini and bare boards ([e6de6d4](https://github.com/gufranco/openscex-modchip/commit/e6de6d4b1f20e110541c0016b0c489e6ae457ae3))

# [0.12.0](https://github.com/gufranco/openscex-modchip/compare/v0.11.0...v0.12.0) (2026-10-09)


### Features

* debug and final indicator profiles ([a446fda](https://github.com/gufranco/openscex-modchip/commit/a446fda0c1e72ff4afc6fb8157b70180c7a896af))

# [0.11.0](https://github.com/gufranco/openscex-modchip/compare/v0.10.4...v0.11.0) (2026-10-09)


### Features

* faster, steadier per-console calibration ([011baaf](https://github.com/gufranco/openscex-modchip/commit/011baafd44a5deab44297cb268eba29d8f0285c5))

## [0.10.4](https://github.com/gufranco/openscex-modchip/compare/v0.10.3...v0.10.4) (2026-10-09)


### Bug Fixes

* no trim sample across an injected string ([0e1429f](https://github.com/gufranco/openscex-modchip/commit/0e1429f88799b2d999604b75bfc67985db003780))
* serve a lead-in re-read after play ([7b0efe3](https://github.com/gufranco/openscex-modchip/commit/7b0efe3f6232e7284d7cc11c5553877443aef4fe))

## [0.10.3](https://github.com/gufranco/openscex-modchip/compare/v0.10.2...v0.10.3) (2026-10-08)


### Bug Fixes

* keep the board after a watchdog reset ([5834986](https://github.com/gufranco/openscex-modchip/commit/5834986e02c1da144ccf00ac24f62ab656ba0f7b))

## [0.10.2](https://github.com/gufranco/openscex-modchip/compare/v0.10.1...v0.10.2) (2026-10-08)


### Bug Fixes

* make the SQCK gap check last 1 ms ([e10d4dc](https://github.com/gufranco/openscex-modchip/commit/e10d4dc82afc1d1c43fc6a53a562037f8e7e3bde))

## [0.10.1](https://github.com/gufranco/openscex-modchip/compare/v0.10.0...v0.10.1) (2026-10-07)


### Bug Fixes

* hold the burst through a low supply reading ([e781eae](https://github.com/gufranco/openscex-modchip/commit/e781eae5c288f58118fd619aafe9875c41daf57b))
* set the supply limit for a high bandgap ([c269a02](https://github.com/gufranco/openscex-modchip/commit/c269a028931c85d363377f5907d7eb6311d513e4))

# [0.10.0](https://github.com/gufranco/openscex-modchip/compare/v0.9.0...v0.10.0) (2026-10-07)


### Features

* confirm gate boards before each string ([a3578ce](https://github.com/gufranco/openscex-modchip/commit/a3578ce4c20fa643c860bd2270ca996fcb5cb141))
* hold strings on a low supply ([ede183b](https://github.com/gufranco/openscex-modchip/commit/ede183bbd1c7d0d0dca11b7770e8493f311a7b95))

# [0.9.0](https://github.com/gufranco/openscex-modchip/compare/v0.8.0...v0.9.0) (2026-10-07)


### Features

* target the attiny85 only ([ed81a23](https://github.com/gufranco/openscex-modchip/commit/ed81a2361d43dc49d244552417332b95188ae3ff))

# [0.8.0](https://github.com/gufranco/openscex-modchip/compare/v0.7.0...v0.8.0) (2026-10-07)


### Features

* drop the lid wire, see swaps in subq ([a63d158](https://github.com/gufranco/openscex-modchip/commit/a63d15835507f5b9157b971638592dc99b4dc4a3))

# [0.7.0](https://github.com/gufranco/openscex-modchip/compare/v0.6.0...v0.7.0) (2026-10-07)


### Features

* internal oscillator trim, pu-7 and pu-8 ([9f10e6e](https://github.com/gufranco/openscex-modchip/commit/9f10e6e91b709472c086f1db5ac88cb12af160a4))

# [0.6.0](https://github.com/gufranco/openscex-modchip/compare/v0.5.0...v0.6.0) (2026-10-07)


### Features

* string gap, gate drive and jp start ([611c140](https://github.com/gufranco/openscex-modchip/commit/611c1409eedf9d9b5d68ead510b9d9fb945a30bb))

# [0.5.0](https://github.com/gufranco/openscex-modchip/compare/v0.4.0...v0.5.0) (2026-10-07)


### Features

* learn per-console string cap and start ([9d5e043](https://github.com/gufranco/openscex-modchip/commit/9d5e04319491bb19d3264384547c1e57d8d65f89))

# [0.4.0](https://github.com/gufranco/openscex-modchip/compare/v0.3.1...v0.4.0) (2026-10-07)


### Features

* led stage and result codes ([ed605bd](https://github.com/gufranco/openscex-modchip/commit/ed605bd966f0f591591f7b78448f521c3fb19467))

## [0.3.1](https://github.com/gufranco/openscex-modchip/compare/v0.3.0...v0.3.1) (2026-10-07)


### Bug Fixes

* **capture:** follow a fast sqck in assembly ([2570f18](https://github.com/gufranco/openscex-modchip/commit/2570f18dd8dcf6a9135143eef3bc260608d55ae4))
* **diag:** record a failed disc when the lid opens ([c26e6b5](https://github.com/gufranco/openscex-modchip/commit/c26e6b52e5dcf7c5351a90c3075796bb52899a74))

# [0.3.0](https://github.com/gufranco/openscex-modchip/compare/v0.2.0...v0.3.0) (2026-10-06)


### Features

* attiny84 on the console clock, lid wire ([af33cd7](https://github.com/gufranco/openscex-modchip/commit/af33cd70fa35eda57ad075d4c79abb0be7b16d63))

# [0.2.0](https://github.com/gufranco/openscex-modchip/compare/v0.1.0...v0.2.0) (2026-10-06)


### Features

* **stealth:** stop once the console accepts ([c3723ac](https://github.com/gufranco/openscex-modchip/commit/c3723ac778b52889f7c0d2e4d249291fa5921bb2))

# [0.1.0](https://github.com/gufranco/openscex-modchip/compare/v0.0.1...v0.1.0) (2026-10-06)


### Features

* **subq:** add the SCPH-5903 video CD filter ([601b0fc](https://github.com/gufranco/openscex-modchip/commit/601b0fce14fdde4ffc43dde766e82ea32e2f2621))

## [0.0.1](https://github.com/gufranco/openscex-modchip/compare/v0.0.0...v0.0.1) (2026-10-06)


### Bug Fixes

* **bios:** bound patch waits, fall back to SCEx ([b95f5f9](https://github.com/gufranco/openscex-modchip/commit/b95f5f9011b69208a7e810e7e97a1822e8289967))
* **bios:** use each model's own patch constants ([2c14737](https://github.com/gufranco/openscex-modchip/commit/2c14737e90e1e716a670c5156275d599473c07f2))
* **diag:** log each session, skip unchanged bytes ([b44e584](https://github.com/gufranco/openscex-modchip/commit/b44e584d243ea303b3e2d21641d0f55294978462))
* **engine:** let WFCK settle before board detect ([ff298fe](https://github.com/gufranco/openscex-modchip/commit/ff298fe7946bb3971820e67f686a263266c5633a))
* **engine:** resync SUBQ capture on the frame gap ([b4fe8b4](https://github.com/gufranco/openscex-modchip/commit/b4fe8b452b1141d6fd28912a088a25536f5b52b4))
* keep the new wait loops MISRA-clean ([f4c3c19](https://github.com/gufranco/openscex-modchip/commit/f4c3c19a1a716a312b373d14978a6a4c79e8858b)), closes [#if](https://github.com/gufranco/openscex-modchip/issues/if)
* **port:** reset the chip when WFCK stalls ([c2b89fa](https://github.com/gufranco/openscex-modchip/commit/c2b89fa0c5aa809e499319745d05ae2370d543fe))
* **subq:** admit lead-in minutes 98 and 99 ([4270f07](https://github.com/gufranco/openscex-modchip/commit/4270f07d6a262a1c07d46b4535e408e315b3b6c4))
* **subq:** detect the program area by track number ([b8e61eb](https://github.com/gufranco/openscex-modchip/commit/b8e61eb34880b551e85f110a051e9d1c1bb42214))
