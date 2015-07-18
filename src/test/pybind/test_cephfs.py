# vim: expandtab smarttab shiftwidth=4 softtabstop=4
from nose.tools import assert_raises, assert_equal
import cephfs as libcephfs

cephfs = None

def setup_module():
    global cephfs
    cephfs = libcephfs.LibCephFS(conffile='')
    cephfs.mount()

def teardown_module():
    global cephfs
    cephfs.shutdown()

def test_version():
    cephfs.version()

def test_statfs():
    stat = cephfs.statfs('/')
    assert(len(stat) == 11)

def test_syncfs():
    stat = cephfs.sync_fs()

def test_directory():
    cephfs.mkdir("/temp-directory", 0755)
    cephfs.chdir("/temp-directory")
    assert_equal(cephfs.getcwd(), "/temp-directory")
    cephfs.rmdir("/temp-directory")
    assert_raises(libcephfs.ObjectNotFound, cephfs.chdir, "/temp-directory")

def test_walk_dir():
    cephfs.chdir("/")
    dirs = ["dir-1", "dir-2", "dir-3"]
    for i in dirs:
        cephfs.mkdir(i, 0755)
    handler = cephfs.opendir("/")
    d = cephfs.readdir(handler)
    dirs += [".", ".."]
    while d:
        assert(d.d_name in dirs)
        dirs.remove(d.d_name)
        d = cephfs.readdir(handler)
    assert(len(dirs) == 0)
    dirs = ["/dir-1", "/dir-2", "/dir-3"]
    for i in dirs:
        cephfs.rmdir(i)
    cephfs.closedir(handler)

def test_xattr():
    assert_raises(libcephfs.OperationNotSupported, cephfs.setxattr, "/", "key", "value", 0)
    cephfs.setxattr("/", "user.key", "value", 0)
    assert_equal("value", cephfs.getxattr("/", "user.key"))

def test_rename():
    cephfs.mkdir("/a", 0755)
    cephfs.mkdir("/a/b", 0755)
    cephfs.rename("/a", "/b")
    cephfs.stat("/b/b")
    cephfs.rmdir("/b/b")
    cephfs.rmdir("/b")

def test_open():
    assert_raises(libcephfs.ObjectNotFound, cephfs.open, 'file-1', 'r')
    assert_raises(libcephfs.ObjectNotFound, cephfs.open, 'file-1', 'r+')
    fd = cephfs.open('file-1', 'w')
    cephfs.write(fd, "asdf", 0)
    cephfs.close(fd)
    fd = cephfs.open('file-1', 'r')
    assert_equal(cephfs.read(fd, 0, 4), "asdf")
    cephfs.close(fd)
    fd = cephfs.open('file-1', 'r+')
    cephfs.write(fd, "zxcv", 4)
    assert_equal(cephfs.read(fd, 4, 8), "zxcv")
    cephfs.close(fd)
    fd = cephfs.open('file-1', 'w+')
    assert_equal(cephfs.read(fd, 0, 4), "")
    cephfs.write(fd, "zxcv", 4)
    assert_equal(cephfs.read(fd, 4, 8), "zxcv")
    cephfs.close(fd)
    assert_raises(libcephfs.OperationNotSupported, cephfs.open, 'file-1', 'a')
    cephfs.unlink('file-1')
