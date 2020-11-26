FROM papawattu/build-firmware
ENV IDF_PATH /esp/esp-idf
ENV BASH_ENV "/root/.bashrc"
RUN echo "export IDF_PATH=/esp/esp-idf" >> /root/.bashrc
RUN echo "source /esp/esp-idf/export.sh" >> /root/.bashrc
ENTRYPOINT ["/bin/bash","-c"]
