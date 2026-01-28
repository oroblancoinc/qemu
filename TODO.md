# TODO

## CI/CD Issues

### macOS builds failing - runners retired

The macOS builds are failing because GitHub has retired the macOS-13 runner images.

**Affected jobs:**
- Build (x86_64-apple-darwin, xtensa-softmmu)
- Build (x86_64-apple-darwin, riscv32-softmmu)
- Build (aarch64-apple-darwin, xtensa-softmmu)
- Build (aarch64-apple-darwin, riscv32-softmmu)

**Error:**
```
The macOS-13 based runner images are now retired. For more details, see https://github.com/actions/runner-images/issues/13046.
```

**Fix:**
Update `.github/workflows/build.yml` to use newer macOS runners:
- `macos-13` → `macos-14` or `macos-15` for x86_64
- `macos-13-xlarge` → `macos-14-xlarge` or `macos-15-xlarge` for aarch64

See: https://github.com/actions/runner-images/issues/13046

### Linux build intermittent failure

One Linux build job occasionally fails with exit code 127 (command not found) during prerequisites installation.

**Affected job:**
- Build (x86_64-linux-gnu, riscv32-softmmu)

**Error:**
```
Process completed with exit code 127.
```

**Investigation needed:**
- Check `.github/workflows/scripts/prerequisites-native.sh` for missing commands
- May be a transient issue with the Debian 11 container
