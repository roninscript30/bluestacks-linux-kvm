.PHONY: all build install run tune status clean

all: build

build:
	@./scripts/build.sh

install: build
	@./scripts/install.sh

run:
	@./scripts/launch.sh

tune:
	@./scripts/guest-tune.sh

status:
	@./bluestacks-kvm status

clean:
	rm -rf dist/*.dll dist/*.exe dist/*.lib dist/*.obj
