# ===========================================================================
# Stage 1: Emscripten Build
# ===========================================================================
FROM emscripten/emsdk:latest AS builder
WORKDIR /app
COPY . .

RUN emcmake cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=ON       \
    -DEMSCRIPTEN=ON             \
    -DBUILD_SIMPLE=ON
RUN cmake --build build -j$(nproc)

# ===========================================================================
# Stage 2: Lightweight Web Server
# ===========================================================================
FROM nginx:alpine

COPY --from=builder /app/build/index.html /usr/share/nginx/html/
COPY --from=builder /app/build/index.js /usr/share/nginx/html/
COPY --from=builder /app/build/index.wasm /usr/share/nginx/html/
COPY --from=builder /app/build/app.css /usr/share/nginx/html/
COPY --from=builder /app/build/app.js /usr/share/nginx/html/
COPY --from=builder /app/build/coi-serviceworker.js /usr/share/nginx/html/

EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
