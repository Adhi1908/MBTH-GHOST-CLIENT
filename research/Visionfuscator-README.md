# Visionfuscator

Visionfuscator is a Java 21 JAR obfuscator built for Minecraft mods and other Java applications. It focuses on practical release protection while keeping runtime compatibility in mind for Fabric and similar mod-loader environments. It is currently NOT Opensource.

**Purchase Source: $50**

## Features

- Class name obfuscation
- Optional member renaming
- String encryption for method literals
- Encryption of `static final String` constants
- Common `invokedynamic` string concat rewriting
- Number obfuscation for integer constants
- Debug metadata stripping
- Conservative control-flow obfuscation
- Optional control-flow flattening
- Mapping file output for debugging obfuscated releases
- Fabric entrypoint and mixin exclusion discovery
- Manifest `Main-Class` remapping
- Resource and `META-INF` preservation
- Runtime decryptor injection

## Why Visionfuscator

Visionfuscator is designed to make reverse engineering more annoying without immediately breaking the mod at runtime. It is especially useful for Minecraft clients and mods where aggressive renaming can easily break mixins, reflection, entrypoints, or event systems.

The safest setup for Minecraft mods is usually:

- class renaming enabled
- member renaming disabled
- string encryption enabled
- debug stripping enabled
- conservative flow obfuscation enabled

## Requirements

- Java 21
- ASM libraries in `lib/`
- Windows, Linux, or macOS with Java available

## Project Layout

- `src/main/java/obfuscator/` - source code
- `lib/` - ASM dependencies
- `build/classes/` - compiled classes
- `MANIFEST.MF` - runnable JAR manifest
- `obfuscator.properties.example` - example config
- `safe-test.properties` - safer Minecraft/Fabric config

## Build

Compile the project:

```bash
javac --release 21 -cp "lib/*" -d build/classes src/main/java/obfuscator/*.java
