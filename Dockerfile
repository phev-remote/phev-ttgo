FROM papawattu/build-firmware
ENV IDF_PATH /esp/esp-idf
RUN echo "IDF_PATH=/esp/esp-idf" >> /etc/profile
RUN cp /esp/esp-idf/export.sh /etc/profile.d
CMD ["/bin/bash","--login","-c","/esp/esp-idf/tools/idf.py build"]
