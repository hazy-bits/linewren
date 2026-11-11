{
  description = "Linewren, a focused prose editor for classic Mac OS and PowerPC Mac OS X";

  inputs = {
    Retro68.url = "github:autc04/Retro68";
    nixpkgs.follows = "Retro68/nixpkgs";
    flake-parts.url = "github:hercules-ci/flake-parts";
    classicMacApp.url = "github:hazy-bits/classic-mac-app-template";
    classicMacApp.inputs.Retro68.follows = "Retro68";
    classicMacApp.inputs.flake-parts.follows = "flake-parts";
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
            (inputs.classicMacApp.lib.mkClassicMacDerivation {
              inherit pkgs architecture suffix cmakeFlags;
              retro68 = retro;
              source = ./.;
              applicationName = "Linewren-${version}";
              interfaceMode = "universal-only";
              extraNativeBuildInputs = [ pkgs.python3 ];
            }).overrideAttrs {
              postInstall = ''
                python3 ${./scripts/mark-finder-bundle.py} "$out"
              '';
            };
          powerpc = build "powerpc" "" [ ];
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
            inherit (inputs'.classicMacApp.packages) classic-runner;
            distribution = pkgs.runCommand "Linewren-${version}-Mac-OS-9" { nativeBuildInputs = [ pkgs.zip ]; } ''
              mkdir -p "$out/Linewren/Fonts"
              cp ${powerpc}/Linewren.bin "$out/Linewren/"
              cp ${./LICENSE} "$out/Linewren/LICENSE.txt"
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

          apps = { inherit (inputs'.classicMacApp.apps) classic; };
          devShells.default = powerpc;
          formatter = pkgs.nixfmt-rfc-style;
        };
    };
}
