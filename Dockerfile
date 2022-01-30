FROM mariadb/maxscale:6.2.1 as builder

RUN yum install -y python36
ADD ./BUILD/ /src/BUILD/
RUN /src/BUILD/install_build_deps.sh
ADD ./ /src/
RUN mkdir -p /build && cd /build && cmake /src -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTS= -DPACKAGE=Y && make -j$(nproc) package

FROM mariadb/maxscale:6.2.1

COPY --from=builder /build/*.rpm /
RUN rpm -U /*.rpm
