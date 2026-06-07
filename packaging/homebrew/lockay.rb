class Lockay < Formula
  desc "chmod for lines of code. sudo for autonomous agents."
  homepage "https://github.com/Steven-ZN/lockay"
  url "https://github.com/Steven-ZN/lockay/archive/refs/heads/main.tar.gz"
  sha256 "" # run: curl -sL <url> | shasum -a 256
  license "MIT"
  version "0.1.0"

  depends_on "gcc" => :build
  depends_on "make" => :build

  def install
    system "make", "clean"
    system "make"
    bin.install "build/lockay"
  end

  test do
    system "#{bin}/lockay", "--help"
  end
end
