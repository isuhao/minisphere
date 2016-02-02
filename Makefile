PKG_NAME=minisphere-$(shell cat VERSION)

.PHONY: all
all: minisphere cell ssj

.PHONY: minisphere
minisphere: bin/sphere

.PHONY: cell
cell: bin/cell

.PHONY: ssj
ssj: bin/ssj

.PHONY: dist
dist: all
	mkdir -p dist/$(PKG_NAME)
	cp -r assets man src dist/$(PKG_NAME)
	cp Makefile VERSION dist/$(PKG_NAME)
	cp CHANGELOG LICENSE.txt README.md dist/$(PKG_NAME)
	cd dist && tar cfz $(PKG_NAME).tar.gz $(PKG_NAME)

.PHONY: install
install: all
	mkdir -p /usr/local/share/minisphere
	cp bin/sphere bin/cell bin/ssj /usr/local/bin
	cp -r bin/system /usr/local/share/minisphere
	mkdir -p /usr/local/share/man/man1
	gzip man/sphere.1 -c > /usr/local/share/man/man1/sphere.1.gz

.PHONY: clean
clean:
	rm -f bin/sphere bin/cell bin/ssj

bin/sphere:
	mkdir -p bin
	cc -o bin/sphere -O3 -Isrc/shared -Isrc/minisphere \
	   src/minisphere/main.c \
	   src/shared/duktape.c src/shared/dyad.c src/shared/mt19937ar.c \
	   src/shared/lstring.c src/shared/path.c src/shared/unicode.c \
	   src/shared/vector.c \
	   src/minisphere/animation.c src/minisphere/api.c \
	   src/minisphere/async.c src/minisphere/atlas.c \
	   src/minisphere/audialis.c src/minisphere/bytearray.c \
	   src/minisphere/color.c src/minisphere/console.c \
	   src/minisphere/debugger.c src/minisphere/file.c \
	   src/minisphere/font.c src/minisphere/galileo.c \
	   src/minisphere/geometry.c src/minisphere/image.c \
	   src/minisphere/input.c src/minisphere/logger.c \
	   src/minisphere/map_engine.c src/minisphere/obsmap.c \
	   src/minisphere/persons.c src/minisphere/physics.c \
	   src/minisphere/primitives.c src/minisphere/rng.c \
	   src/minisphere/script.c src/minisphere/shader.c \
	   src/minisphere/sockets.c src/minisphere/spherefs.c \
	   src/minisphere/spk.c src/minisphere/spriteset.c \
	   src/minisphere/surface.c src/minisphere/tileset.c \
	   src/minisphere/utility.c src/minisphere/windowstyle.c \
	   -lallegro_acodec -lallegro_audio -lallegro_color -lallegro_dialog \
	   -lallegro_image -lallegro_memfile -lallegro_primitives -lallegro \
	   -lmng -lz -lm
	cp -r assets/system bin

bin/cell:
	mkdir -p bin
	cc -o bin/cell -O3 -Isrc/shared \
	   src/cell/main.c \
	   src/shared/duktape.c src/shared/path.c src/shared/vector.c \
	   src/cell/assets.c src/cell/build.c src/cell/spk_writer.c \
	   src/cell/utility.c \
	   -lz -lm

bin/ssj:
	mkdir -p bin
	cc -o bin/ssj -O3 -Isrc/shared \
	   src/ssj/main.c \
	   src/shared/dyad.c src/shared/lstring.c src/shared/path.c \
	   src/shared/unicode.c src/shared/vector.c \
	   src/ssj/remote.c src/ssj/session.c src/ssj/source.c