## clone the repo
```bash
git clone git@github.com:richinsley/pyshadertranslator.git
```

## build with docker image
```bash
docker run -it \
  -e HOST_UID=$(id -u) \
  -e HOST_GID=$(id -g) \
  -v "$(pwd):/workspace" \
  pyshadertranslator-builder
```

## setup the tools and gather the angle repo
```bash
setup.sh
```

## build the wasm file
```bash
build.sh
```

## build package
```bash
# install build dependencies
pip install build twine

# uninstall previous
pip uninstall py-webgl-transpiler

# build and test install wheel
python -m build
pip install dist/py_webgl_transpiler-0.1.0-py3-none-any.whl
```