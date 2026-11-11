{
  description = "Linewren, a focused prose editor for classic Mac OS and PowerPC Mac OS X";

  inputs = {
    Retro68.url = "github:autc04/Retro68";
    nixpkgs.follows = "Retro68/nixpkgs";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs =
    inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
        "x86_64-darwin"
      ];

      perSystem =
        { inputs', pkgs, ... }:
        let
          retro = inputs'.Retro68;
          tools = retro.packages.tools;
          version = pkgs.lib.removeSuffix "\n" (builtins.readFile ./VERSION);
          classicArchive = "Linewren-${version}-Mac-OS-9.zip";
          build =
            architecture: suffix: cmakeFlags:
            let
              target = retro.legacyPackages.pkgsCross.${architecture};
              base = target.stdenvUniversal;
              stdenv = base.override {
                cc = base.cc.override {
                  extraPackages = with target.retro68; [
                    universal
                    libretro
                    setup_hook
                  ];
                };
              };
            in
            stdenv.mkDerivation {
              name = "Linewren-${version}" + suffix;
              src = ./.;
              inherit cmakeFlags;
              nativeBuildInputs = [
                pkgs.cmake
                pkgs.ninja
                pkgs.python3
                tools
              ];
              postInstall = ''
                python3 ${./scripts/mark-finder-bundle.py} "$out"
              '';
            };
          powerpc = build "powerpc" "" [ ];
          runner = pkgs.writeShellApplication {
            name = "classic";
            text = ''
              exec ${pkgs.python3}/bin/python3 ${./vendor/classic-mac-app/classic.py} "$@"
            '';
          };
        in
        {
          packages = {
            inherit powerpc;
            default = powerpc;
            powerpc-smoke = build "powerpc" "-smoke" [ "-DLINEWREN_SMOKE_TEST=ON" ];
            powerpc-osx = build "carbon" "-osx" [ ];
            powerpc-osx-leopard = build "carbon" "-osx-leopard" [ "-DLINEWREN_OSX_ICONS=leopard" ];
            powerpc-osx-smoke = build "carbon" "-osx-smoke" [ "-DLINEWREN_SMOKE_TEST=ON" ];
            powerpc-font-specimens = build "powerpc" "-font-specimens" [ "-DLINEWREN_FONT_SPECIMENS=ON" ];
            retro68-tools = tools;
            classic-runner = runner;
            distribution = pkgs.runCommand "Linewren-${version}-Mac-OS-9" { nativeBuildInputs = [ pkgs.zip ]; } ''
              mkdir -p "$out/Linewren/Fonts"
              cp ${powerpc}/Linewren.bin "$out/Linewren/"
              cp ${./fonts}/* "$out/Linewren/Fonts/"
              cd "$out"
              zip -qr ${pkgs.lib.escapeShellArg classicArchive} Linewren
            '';
          };

          checks.local = pkgs.runCommand "linewren-local-tests" { nativeBuildInputs = [ pkgs.stdenv.cc ]; } ''
            cp -R ${./.} source
            chmod -R u+w source
            cd source
            ./scripts/test-local.sh
            touch "$out"
          '';

          apps.classic = {
            type = "app";
            program = "${runner}/bin/classic";
          };
          devShells.default = powerpc;
          formatter = pkgs.nixfmt-rfc-style;
        };
    };
}
