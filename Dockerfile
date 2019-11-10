FROM ubuntu
RUN apt-get -y update && apt-get -y install git wget libncurses-dev flex bison gperf python python-pip python-setuptools python-serial python-click python-cryptography python-future python-pyparsing python-pyelftools cmake ninja-build ccache
WORKDIR /esp
ENV HOME /esp
RUN git clone --recursive https://github.com/espressif/esp-idf.git
RUN cd esp-idf && ./install.sh
RUN chmod +x /esp/esp-idf/export.sh
RUN echo ". $HOME/esp/esp-idf/export.sh" >> $HOME/.bash_profile
ENV IDF_PATH $HOME/esp-idf
ENV PATH $IDF_PATH/tools:$HOME/.espressif/tools/xtensa-esp32-elf/esp32-2019r1-8.2.0/xtensa-esp32-elf/bin:$PATH
WORKDIR /src

