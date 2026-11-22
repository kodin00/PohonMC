# PohonMC Launcher

PohonMC Launcher adalah fork dari [PineconeMC](https://github.com/ElyPrismLauncher/Launcher), yang juga merupakan fork dari Prism Launcher.

<p align="center">
  <img alt="Banner PohonMC Launcher bergambar pohon voxel yang diterangi lentera" src="./docs/assets/pohonmc-banner.jpg" width="1000">
  <br><a href="https://kodin00.github.io/PohonMC/#downloads">Unduh PohonMC</a> · <a href="https://kodin00.github.io/PohonMC/">Dokumentasi</a>
</p>

<p align="center">
  <b>Fork</b> Prism Launcher ini menambahkan dukungan bawaan untuk akun Ely.by (akun MSA tetap bisa dipakai)<br />
  <br />Proyek ini <b>tidak</b> didukung atau disahkan oleh Prism Launcher maupun Ely.by.
</p>

## Instalasi

- Semua unduhan dan petunjuk pemasangan PohonMC Launcher ada di halaman [Unduhan](https://kodin00.github.io/PohonMC/#downloads).
- Status build terakhir bisa dilihat di tab [GitHub Actions](https://github.com/kodin00/PohonMC/actions).

### Build Pengembangan

Build ini tidak ditujukan untuk kebanyakan pengguna. Bisa saja ada bug atau perilaku yang belum stabil, jadi pakai dengan risiko sendiri.

Build pengembangan tersedia lewat:

- [GitHub Actions](https://github.com/kodin00/PohonMC/actions), termasuk build dari pull request yang dibuka kontributor
- [nightly.link](https://nightly.link/kodin00/PohonMC/workflows/build/develop), yang selalu mengarah ke versi terbaru branch develop

Binary pada build ini menyertakan informasi debug, sehingga ukuran berkasnya lebih besar.

## Berbagi Modpack

1. Buat instance Minecraft dan tambahkan mod yang kamu inginkan.
2. Pilih instance tersebut, lalu buka **Export → Share Modpack**. Tandai mod yang wajib dimiliki pemain untuk masuk ke servermu sebagai **Required**, termasuk dependensi wajibnya. Kamu juga bisa mengelompokkan mod yang dipilih lewat **Edit → Mods → Server Mods**. Kolom **Server Requirement** di daftar mod menunjukkan mod mana yang ditandai Required atau Optional. Urutkan berdasarkan kolom itu supaya mod yang wajib untuk server muncul paling atas.
3. Klik **Copy Share Code** dan kirim kodenya ke pemain lain. Kode itu memuat versi Minecraft yang persis sama, mod loader beserta versinya, dan daftar berkas mod. Tiap launcher mengunduh berkasnya langsung dari penyedia aslinya.
4. Di launcher penerima, buka **Add Instance → Share Code**, tempel kodenya, lalu klik **Preview Modpack**. Pakai **Show only server-required mods** untuk menyaring pratinjau. Pilih **Install All Mods** atau **Install Server-Required Mods Only**. Pemasangan khusus mod wajib akan mengaktifkan mod yang dipilih, termasuk mod yang dinonaktifkan pembuatnya di komputernya sendiri.

PohonMC mengunduh berkas Modrinth dan CurseForge yang didukung secara otomatis. Berkas yang tidak punya sumber unduhan yang didukung tetap tercantum di share code dan tampil sebagai **Missing** di launcher penerima. Kirim berkas itu secara terpisah supaya pemain lain bisa menaruhnya di folder Mods milik instance tersebut.

## Build dari Kode Sumber

Pasang dulu dependensi yang tercantum di [petunjuk build](https://prismlauncher.org/wiki/development/build-instructions) Prism Launcher, lalu gunakan Makefile di root repositori:

- `make build`: konfigurasi dan build untuk sistem operasi yang sedang dipakai.
- `make run`: build, lalu jalankan PohonMC Launcher.
- `make test`: build dan jalankan rangkaian tes dengan `ctest`.
- `make package`: buat paket siap pasang, berupa app bundle di macOS atau `.deb` lewat `make package deb`.
- `make clean`: hapus direktori `build/`.
- `make help`: tampilkan daftar target yang tersedia.

Platform dideteksi otomatis dari sistem yang dipakai. Kamu bisa menggantinya dengan `PLATFORM=macos|linux|windows` atau menuliskannya langsung setelah target, misalnya `make build macos`. Jenis build diatur dengan `CONFIG=Debug|Release`, dengan `Debug` sebagai bawaan. Makefile ini menjalankan preset CMake di `CMakePresets.json`, dan semua hasil build disimpan di `build/`.

## Bagian berikut berasal dari README asli Prism Launcher

### Kebijakan fork, redistribusi, dan build kustom

Kamu bebas membuat fork, mendistribusikan ulang, dan menyediakan build kustom selama mengikuti ketentuan [lisensi](LICENSE). Ini kewajiban hukum. Kalau kamu mengubah kodenya dan tidak sekadar mengemas build kustom, mohon lakukan hal berikut sebagai bentuk sopan santun:

- Jelaskan bahwa fork milikmu bukan Prism Launcher dan tidak didukung oleh atau berafiliasi dengan proyek Prism Launcher (<https://prismlauncher.org>).
- Periksa [CMakeLists.txt](CMakeLists.txt) dan ganti API key milik Prism Launcher dengan milikmu sendiri, atau kosongkan nilainya (`""`) untuk menonaktifkannya. Dengan begitu program tetap bisa dikompilasi, tetapi fitur yang membutuhkan key tersebut tidak aktif.

Kalau ada pertanyaan atau butuh penjelasan soal ketentuan di atas, silakan buat issue.

Kalau kamu hanya membuat build Prism Launcher untuk distribusimu, pastikan `Launcher_BUILD_PLATFORM` diisi dengan slug yang mewakili distribusi tersebut. Contohnya `archlinux`, `fedora`, dan `nixpkgs`.

Perlu diingat, kalau kamu membuat build perangkat lunak ini tanpa menghapus API key yang disediakan di [CMakeLists.txt](CMakeLists.txt), kamu dianggap menyetujui syarat dan ketentuan berikut:

- [Microsoft Identity Platform Terms of Use](https://docs.microsoft.com/en-us/legal/microsoft-identity-platform/terms-of-use)
- [CurseForge 3rd Party API Terms and Conditions](https://support.curseforge.com/en/support/solutions/articles/9000207405-curse-forge-3rd-party-api-terms-and-conditions)

Kalau kamu tidak setuju dengan syarat dan ketentuan tersebut, hapus API key terkait dari berkas [CMakeLists.txt](CMakeLists.txt) dengan mengosongkan nilainya (`""`).

### Lisensi [![https://github.com/PrismLauncher/PrismLauncher/blob/develop/LICENSE](https://img.shields.io/github/license/PrismLauncher/PrismLauncher?label=License&logo=gnu&color=C4282D)](LICENSE)

Seluruh kode launcher tersedia di bawah lisensi GPL-3.0-only.

Logo dan aset terkait berada di bawah lisensi CC BY-SA 4.0.
