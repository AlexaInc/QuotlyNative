# ──────────────────────────────────────────────
# Build stage – Ubuntu 22.04 (Jammy) ships a
# prebuilt libtdlib-dev in the universe repo.
# No TDLib compilation required.
# ──────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Enable universe (contains libtdlib-dev) and install all build deps + TDLib
RUN apt-get update && apt-get install -y \
    software-properties-common && \
    add-apt-repository universe && \
    apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    unzip \
    pkg-config \
    ca-certificates \
    libcairo2-dev \
    libpango1.0-dev \
    nlohmann-json3-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libasio-dev \
    libboost-all-dev \
    zlib1g-dev \
    --no-install-recommends && \
    rm -rf /var/lib/apt/lists/*

# 1. EXTRACT PREBUILT NATIVE BINARIES FROM THE NUGET PACKAGE (No building!)
WORKDIR /tmp
RUN curl -L -o tdlib.zip https://www.nuget.org/api/v2/package/tdlib.native.linux-x64/1.8.63 && \
    unzip -q tdlib.zip -d tdlib_extracted && \
    cp -r tdlib_extracted/runtimes/linux-x64/native/* /usr/local/lib/ && \
    rm -rf tdlib.zip tdlib_extracted

# 2. GRAB ONLY THE HEADER INTERFACES FROM GITHUB (No building!)
RUN GIT_SSL_NO_VERIFY=true git clone --depth 1 https://github.com/tdlib/td.git /tmp/td-src && \
    mkdir -p /usr/local/include/td/telegram/ && \
    cp /tmp/td-src/td/telegram/td_json_client.h /usr/local/include/td/telegram/ && \
    cp /tmp/td-src/td/telegram/td_log.h /usr/local/include/td/telegram/ && \
    rm -rf /tmp/td-src

# Download Crow (C++ Web Framework) header
RUN wget -q -O /usr/include/crow.h https://github.com/CrowCpp/Crow/releases/download/v1.0+5/crow_all.h

# Clone and build our own app
WORKDIR /app
RUN git clone https://github.com/AlexaInc/QuotlyNative.git .

RUN mkdir build && cd build && \
    cmake .. && \
    make -j$(nproc)

# ──────────────────────────────────────────────
# Final / runtime stage
# ──────────────────────────────────────────────
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    software-properties-common && \
    add-apt-repository universe && \
    apt-get update && apt-get install -y \
    ca-certificates \
    libcairo2 \
    libpango-1.0-0 \
    libpangocairo-1.0-0 \
    fontconfig \
    wget \
    curl \
    unzip \
    # ── Full Font Stack ───────────────────────
    fonts-noto \
    fonts-noto-core \
    fonts-noto-extra \
    fonts-noto-ui-core \
    fonts-noto-ui-extra \
    fonts-noto-cjk \
    fonts-noto-cjk-extra \
    fonts-noto-color-emoji \
    fonts-noto-mono \
    fonts-dejavu \
    fonts-dejavu-core \
    fonts-dejavu-extra \
    fonts-liberation \
    fonts-liberation2 \
    fonts-freefont-ttf \
    fonts-freefont-otf \
    fonts-symbola \
    fonts-ancient-scripts \
    fonts-unifont \
    fonts-arabeyes \
    fonts-hosny-amiri \
    fonts-kacst \
    fonts-kacst-one \
    fonts-sil-scheherazade \
    fonts-sil-lateef \
    fonts-hosny-thabit \
    fonts-farsiweb \
    fonts-nafees \
    fonts-deva \
    fonts-beng \
    fonts-gujr \
    fonts-knda \
    fonts-mlym \
    fonts-orya \
    fonts-guru \
    fonts-taml \
    fonts-telu \
    fonts-lklug-sinhala \
    fonts-lohit-deva \
    fonts-lohit-beng-assamese \
    fonts-lohit-beng-bengali \
    fonts-lohit-gujr \
    fonts-lohit-knda \
    fonts-lohit-mlym \
    fonts-lohit-orya \
    fonts-lohit-guru \
    fonts-lohit-taml \
    fonts-lohit-taml-classical \
    fonts-lohit-telu \
    fonts-pagul \
    fonts-samyak-deva \
    fonts-samyak-gujr \
    fonts-samyak-mlym \
    fonts-samyak-taml \
    fonts-sarai \
    fonts-smc \
    fonts-yrsa-rasa \
    fonts-tibetan-machine \
    fonts-tlwg-garuda \
    fonts-tlwg-kinnari \
    fonts-tlwg-laksaman \
    fonts-tlwg-loma \
    fonts-tlwg-mono \
    fonts-tlwg-norasi \
    fonts-tlwg-purisa \
    fonts-tlwg-sawasdee \
    fonts-tlwg-typewriter \
    fonts-tlwg-typist \
    fonts-tlwg-typo \
    fonts-tlwg-umpush \
    fonts-tlwg-waree \
    fonts-khmeros \
    fonts-lao \
    fonts-sil-padauk \
    fonts-sil-mondulkiri \
    fonts-sil-charis \
    fonts-sil-gentium \
    fonts-sil-gentium-basic \
    fonts-sil-abyssinica \
    fonts-sil-ezra \
    fonts-sil-andika \
    fonts-sil-doulos \
    fonts-arphic-ukai \
    fonts-arphic-uming \
    fonts-vlgothic \
    fonts-takao \
    fonts-takao-gothic \
    fonts-takao-mincho \
    fonts-ipafont \
    fonts-ipafont-gothic \
    fonts-ipafont-mincho \
    fonts-ipaexfont \
    fonts-ipaexfont-gothic \
    fonts-ipaexfont-mincho \
    fonts-unfonts-core \
    fonts-unfonts-extra \
    fonts-nanum \
    fonts-nanum-coding \
    fonts-nanum-extra \
    fonts-baekmuk \
    fonts-wqy-microhei \
    fonts-wqy-zenhei \
    culmus \
    culmus-fancy \
    fonts-bpg-georgian \
    fonts-droid-fallback \
    fonts-roboto \
    fonts-cantarell \
    fonts-open-sans \
    fonts-firacode \
    fonts-jetbrains-mono \
    fonts-inconsolata \
    fonts-mononoki \
    fonts-stix \
    fonts-lyx \
    fonts-texgyre \
    --no-install-recommends && \
    rm -rf /var/lib/apt/lists/*

# Manual Noto downloads (Symbols, Math, Music)
RUN mkdir -p /usr/share/fonts/truetype/noto-manual
ENV NOTO_BASE="https://github.com/googlefonts/noto-fonts/raw/main/hinted/ttf"
RUN set -e; for font in \
    "NotoSansSymbols/NotoSansSymbols-Regular.ttf" \
    "NotoSansSymbols/NotoSansSymbols-Bold.ttf" \
    "NotoSansSymbols/NotoSansSymbols-Light.ttf" \
    "NotoSansSymbols/NotoSansSymbols-Medium.ttf" \
    "NotoSansSymbols/NotoSansSymbols-SemiBold.ttf" \
    "NotoSansSymbols/NotoSansSymbols-Thin.ttf" \
    "NotoSansSymbols2/NotoSansSymbols2-Regular.ttf" \
    "NotoSansMath/NotoSansMath-Regular.ttf" \
    "NotoMusic/NotoMusic-Regular.ttf" \
    ; do \
    wget -q -O "/usr/share/fonts/truetype/noto-manual/$(basename $font)" "${NOTO_BASE}/${font}" || echo "skip $font"; \
    done

# ── EVERY SCRIPT-SPECIFIC NOTO FONT (Full Mirror) ──
RUN set -e; for script in \
    Adlam Ahom AnatolianHieroglyphs Arabic ArabicUI Armenian Avestan \
    Balinese Bamum BassaVah Batak Bengali BengaliUI Bhaiksuki Brahmi \
    Buginese Buhid CanadianAboriginal Carian CaucasianAlbanian Chakma \
    Cham Cherokee Chorasmian Coptic Cuneiform Cypriot CyproMinoan \
    Deseret Devanagari DevanagariUI Dogra Duployan EgyptianHieroglyphs \
    Elbasan Elymaic Ethiopic Georgian Glagolitic Gothic Grantha \
    Gujarati GujaratiUI GunjalaGondi Gurmukhi GurmukhiUI HanifiRohingya \
    Hanunoo Hatran Hebrew ImperialAramaic IndicSiyaqNumbers \
    InscriptionalPahlavi InscriptionalParthian Javanese Kaithi Kannada \
    KannadaUI Kawi KayahLi Kharoshthi Khmer Khojki Khudawadi Lao \
    Lepcha Limbu LinearA LinearB Lisu Lycian Lydian Mahajani Makasar \
    Malayalam MalayalamUI Mandaic Manichaean Marchen MasaramGondi \
    MayanNumerals MedefaidrinScript MeeteiMayek MendeKikakui Meroitic \
    Miao Modi Mongolian Mro Multani Myanmar MyanmarUI Nabataean \
    Nandinagari Newa NewTaiLue NKo NushuPua NyiakengPuachueHmong Ogham \
    OlChiki OldHungarian OldItalic OldNorthArabian OldPermic OldPersian \
    OldSogdian OldSouthArabian OldTurkic OldUyghur Oriya OriyaUI Osage \
    Osmanya PahawhHmong Palmyrene PauCinHau PhagsPa Phoenician \
    PsalterPahlavi Rejang Runic Samaritan Saurashtra Sharada Shavian \
    Siddham SignWriting Sinhala SinhalaUI Sogdian SoraSompeng Soyombo \
    Sundanese SylotiNagri Syriac SyriacEastern SyriacEstrangela \
    SyriacWestern Tagalog Tagbanwa TaiLe TaiTham TaiViet Takri Tamil \
    TamilSupplement TamilUI Tangsa Telugu TeluguUI Thaana Thai \
    Tifinagh TifinaghAdrar TifinaghAgrawImazighen TifinaghAhaggar \
    TifinaghAir TifinaghAPT TifinaghAzawagh TifinaghGhat TifinaghHawad \
    TifinaghRhissaIxa TifinaghSIL TifinaghTawellemmet Tirhuta Ugaritic \
    Vai Vithkuqi Wancho WarangCiti Yezidi Yi Zanabazar \
    ; do \
    wget -q -O "/usr/share/fonts/truetype/noto-manual/NotoSans${script}-Regular.ttf" \
        "${NOTO_BASE}/NotoSans${script}/NotoSans${script}-Regular.ttf" 2>/dev/null || true; \
    done

# Download Inter Font
RUN mkdir -p /usr/share/fonts/truetype/inter && \
    wget -q -O /tmp/inter.zip "https://github.com/rsms/inter/releases/download/v4.0/Inter-4.0.zip" && \
    unzip -qo /tmp/inter.zip -d /tmp/inter 2>/dev/null || true && \
    find /tmp/inter -name "*.ttf" -exec cp {} /usr/share/fonts/truetype/inter/ \; 2>/dev/null || true && \
    rm -rf /tmp/inter /tmp/inter.zip || true

# Rebuild font cache
RUN fc-cache -fv

# Copy compiled shared libs & binaries from builder stage
WORKDIR /app
COPY --from=builder /usr/local/lib/ /usr/local/lib/
COPY --from=builder /usr/local/include/ /usr/local/include/
COPY --from=builder /app/build/quoter ./

RUN ldconfig

EXPOSE 7860

CMD ["./quoter"]
