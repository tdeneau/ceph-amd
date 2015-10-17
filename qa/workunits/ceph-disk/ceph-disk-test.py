#
# Copyright (C) 2015 Red Hat <contact@redhat.com>
#
# Author: Loic Dachary <loic@dachary.org>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Library Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Library Public License for more details.
#
# When debugging these tests (must be root), here are a few useful commands:
#
#  export PATH=..:$PATH
#  ln -sf /home/ubuntu/ceph/src/ceph-disk /usr/sbin/ceph-disk
#  ln -sf /home/ubuntu/ceph/udev/95-ceph-osd.rules /lib/udev/rules.d/95-ceph-osd.rules
#  ln -sf /home/ubuntu/ceph/systemd/ceph-disk@.service /usr/lib/systemd/system/ceph-disk@.service
#  ceph-disk.conf will be silently ignored if it is a symbolic link or a hard link /var/log/upstart for logs
#  cp /home/ubuntu/ceph/src/upstart/ceph-disk.conf /etc/init/ceph-disk.conf
#  python ceph-disk-test.py --verbose --destroy-osd 0
#  py.test -s -v -k test_activate_dmcrypt_luks ceph-disk-test.py
#  udevadm monitor --property & tail -f /var/log/messages # on CentOS 7
#  udevadm monitor --property & tail -f /var/log/syslog /var/log/upstart/*  # on Ubuntu 14.04
#  udevadm test --action=add /block/vdb/vdb1 # verify the udev rule is run as expected
#  udevadm control --reload # when changing the udev rules
#
import argparse
import json
import logging
import os
import pytest
import re
import subprocess
import sys
import tempfile
import time
import uuid

LOG = logging.getLogger('CephDisk')

class CephDisk:

    @staticmethod
    def helper(command):
        command = "ceph-helpers-root.sh " + command
        return CephDisk.sh(command)

    @staticmethod
    def sh(command):
        output = subprocess.check_output(command, shell=True)
        LOG.debug("sh: " + command + ": " + output)
        return output.strip()

    def unused_disks(self, pattern='[vs]d.'):
        names = filter(lambda x: re.match(pattern, x), os.listdir("/sys/block"))
        if not names:
            return []
        disks = json.loads(self.sh("ceph-disk list --format json " + " ".join(names)))
        unused = []
        for disk in disks:
            if 'partitions' not in disk:
                unused.append(disk['path'])
        return unused

    def ensure_sd(self):
        LOG.debug(self.unused_disks('sd.'))
        if self.unused_disks('sd.'):
            return
        modprobe = "modprobe scsi_debug vpd_use_hostno=0 add_host=1 dev_size_mb=200 ; udevadm settle"
        try:
            self.sh(modprobe)
        except:
            self.helper("install linux-image-extra-3.13.0-61-generic")
            self.sh(modprobe)

    def unload_scsi_debug(self):
        self.sh("rmmod scsi_debug || true")

    def get_osd_partition(self, uuid):
        disks = json.loads(self.sh("ceph-disk list --format json"))
        for disk in disks:
            if 'partitions' in disk:
                for partition in disk['partitions']:
                    if partition.get('uuid') == uuid:
                        return partition
        raise Exception("uuid = " + uuid + " not found in " + str(disks))

    def get_journal_partition(self, uuid):
        data_partition = self.get_osd_partition(uuid)
        journal_dev = data_partition['journal_dev']
        disks = json.loads(self.sh("ceph-disk list --format json"))
        for disk in disks:
            if 'partitions' in disk:
                for partition in disk['partitions']:
                    if partition['path'] == journal_dev:
                        if 'journal_for' in partition:
                            assert partition['journal_for'] == data_partition['path']
                        return partition
        raise Exception("journal for uuid = " + uuid + " not found in " + str(disks))

    def destroy_osd(self, uuid):
        id = self.sh("ceph osd create " + uuid)
        self.helper("control_osd stop " + id + " || true")
        self.wait_for_osd_down(uuid)
        try:
            partition = self.get_journal_partition(uuid)
            if partition:
                if partition.get('mount'):
                    self.sh("umount '" + partition['mount'] + "' || true")
                if partition['dmcrypt']:
                    holder = partition['dmcrypt']['holders'][0]
                    self.sh("cryptsetup close $(cat /sys/block/" + holder + "/dm/name) || true")
        except:
            pass
        try:
            partition = self.get_osd_partition(uuid)
            if partition.get('mount'):
                self.sh("umount '" + partition['mount'] + "' || true")
            if partition['dmcrypt']:
                holder = partition['dmcrypt']['holders'][0]
                self.sh("cryptsetup close $(cat /sys/block/" + holder + "/dm/name) || true")
        except:
            pass
        self.sh("""
        ceph osd down {id}
        ceph osd rm {id}
        ceph auth del osd.{id}
        ceph osd crush rm osd.{id}
        """.format(id=id))

    @staticmethod
    def osd_up_predicate(osds, uuid):
        for osd in osds:
            if osd['uuid'] == uuid and 'up' in osd['state']:
                return True
        return False

    @staticmethod
    def wait_for_osd_up(uuid):
        CephDisk.wait_for_osd(uuid, CephDisk.osd_up_predicate, 'up')

    @staticmethod
    def osd_down_predicate(osds, uuid):
        found = False
        for osd in osds:
            if osd['uuid'] == uuid:
                found = True
                if 'down' in osd['state'] or ['exists'] == osd['state']:
                    return True
        return not found

    @staticmethod
    def wait_for_osd_down(uuid):
        CephDisk.wait_for_osd(uuid, CephDisk.osd_down_predicate, 'down')

    @staticmethod
    def wait_for_osd(uuid, predicate, info):
        LOG.info("wait_for_osd " + info + " " + uuid)
        for delay in (1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024):
            dump = json.loads(CephDisk.sh("ceph osd dump -f json"))
            if predicate(dump['osds'], uuid):
                return True
            time.sleep(delay)
        raise Exception('timeout waiting for osd ' + uuid + ' to be ' + info)

    @staticmethod
    def augtool(command):
        return CephDisk.sh("""
        augtool <<'EOF'
        set /augeas/load/IniFile/lens Puppet.lns
        set /augeas/load/IniFile/incl "/etc/ceph/ceph.conf"
        load
        {command}
        save
EOF
        """.format(command=command))

class TestCephDisk(object):

    def setup_class(self):
        logging.basicConfig(level=logging.DEBUG)
        c = CephDisk()
        c.helper("install augeas-tools augeas")
        if c.sh("lsb_release -si") == 'CentOS':
            c.helper("install multipath-tools device-mapper-multipath")
        c.augtool("set /files/etc/ceph/ceph.conf/global/osd_journal_size 100")

    def test_destroy_osd(self):
        c = CephDisk()
        disk = c.unused_disks()[0]
        osd_uuid = str(uuid.uuid1())
        c.sh("ceph-disk prepare --osd-uuid " + osd_uuid + " " + disk)
        c.wait_for_osd_up(osd_uuid)
        partition = c.get_osd_partition(osd_uuid)
        assert partition['type'] == 'data'
        assert partition['state'] == 'active'
        c.destroy_osd(osd_uuid)
        c.sh("ceph-disk zap " + disk)

    def test_augtool(self):
        c = CephDisk()
        out = c.augtool("ls /files/etc/ceph/ceph.conf")
        assert 'global' in out

    def test_activate_dmcrypt_plain(self):
        CephDisk.augtool("set /files/etc/ceph/ceph.conf/global/osd_dmcrypt_type plain")
        self.activate_dmcrypt('plain')
        CephDisk.augtool("rm /files/etc/ceph/ceph.conf/global/osd_dmcrypt_type")

    def test_activate_dmcrypt_luks(self):
        CephDisk.augtool("rm /files/etc/ceph/ceph.conf/global/osd_dmcrypt_type")
        self.activate_dmcrypt('luks')

    def activate_dmcrypt(self, type):
        c = CephDisk()
        disk = c.unused_disks()[0]
        osd_uuid = str(uuid.uuid1())
        journal_uuid = str(uuid.uuid1())
        c.sh("ceph-disk zap " + disk)
        c.sh("ceph-disk prepare " +
             " --osd-uuid " + osd_uuid +
             " --journal-uuid " + journal_uuid +
             " --dmcrypt " +
             " " + disk)
        c.wait_for_osd_up(osd_uuid)
        data_partition = c.get_osd_partition(osd_uuid)
        assert data_partition['type'] == 'data'
        assert data_partition['state'] == 'active'
        journal_partition = c.get_journal_partition(osd_uuid)
        assert journal_partition
        c.destroy_osd(osd_uuid)
        c.sh("ceph-disk zap " + disk)

    def test_activate_no_journal(self):
        c = CephDisk()
        disk = c.unused_disks()[0]
        osd_uuid = str(uuid.uuid1())
        c.sh("ceph-disk zap " + disk)
        c.augtool("set /files/etc/ceph/ceph.conf/global/osd_objectstore memstore")
        c.sh("ceph-disk prepare --osd-uuid " + osd_uuid +
             " " + disk)
        c.wait_for_osd_up(osd_uuid)
        device = json.loads(c.sh("ceph-disk list --format json " + disk))[0]
        assert len(device['partitions']) == 1
        partition = device['partitions'][0]
        assert partition['type'] == 'data'
        assert partition['state'] == 'active'
        assert 'journal_dev' not in partition
        c.helper("pool_read_write")
        c.destroy_osd(osd_uuid)
        c.sh("ceph-disk zap " + disk)
        c.augtool("rm /files/etc/ceph/ceph.conf/global/osd_objectstore")

    def test_activate_with_journal(self):
        c = CephDisk()
        disk = c.unused_disks()[0]
        osd_uuid = str(uuid.uuid1())
        c.sh("ceph-disk zap " + disk)
        c.sh("ceph-disk prepare --osd-uuid " + osd_uuid +
             " " + disk)
        c.wait_for_osd_up(osd_uuid)
        device = json.loads(c.sh("ceph-disk list --format json " + disk))[0]
        assert len(device['partitions']) == 2
        data_partition = c.get_osd_partition(osd_uuid)
        assert data_partition['type'] == 'data'
        assert data_partition['state'] == 'active'
        journal_partition = c.get_journal_partition(osd_uuid)
        assert journal_partition
        c.helper("pool_read_write")
        c.destroy_osd(osd_uuid)
        c.sh("ceph-disk zap " + disk)

    def test_activate_separated_journal(self):
        c = CephDisk()
        disks = c.unused_disks()
        data_disk = disks[0]
        journal_disk = disks[1]
        osd_uuid = self.activate_separated_journal(data_disk, journal_disk)
        c.helper("pool_read_write 1") # 1 == pool size
        c.destroy_osd(osd_uuid)
        c.sh("ceph-disk zap " + data_disk + " " + journal_disk)

    def activate_separated_journal(self, data_disk, journal_disk):
        c = CephDisk()
        osd_uuid = str(uuid.uuid1())
        c.sh("ceph-disk prepare --osd-uuid " + osd_uuid +
             " " + data_disk + " " + journal_disk)
        c.wait_for_osd_up(osd_uuid)
        device = json.loads(c.sh("ceph-disk list --format json " + data_disk))[0]
        assert len(device['partitions']) == 1
        data_partition = c.get_osd_partition(osd_uuid)
        assert data_partition['type'] == 'data'
        assert data_partition['state'] == 'active'
        journal_partition = c.get_journal_partition(osd_uuid)
        assert journal_partition
        return osd_uuid

    #
    # Create an OSD and get a journal partition from a disk that
    # already contains a journal partition which is in use. Updates of
    # the kernel partition table may behave differently when a
    # partition is in use. See http://tracker.ceph.com/issues/7334 for
    # more information.
    #
    def test_activate_two_separated_journal(self):
        c = CephDisk()
        disks = c.unused_disks()
        data_disk = disks[0]
        other_data_disk = disks[1]
        journal_disk = disks[2]
        osd_uuid = self.activate_separated_journal(data_disk, journal_disk)
        other_osd_uuid = self.activate_separated_journal(other_data_disk, journal_disk)
        #
        # read/write can only succeed if the two osds are up because
        # the pool needs two OSD
        #
        c.helper("pool_read_write 2") # 2 == pool size
        c.destroy_osd(osd_uuid)
        c.destroy_osd(other_osd_uuid)
        c.sh("ceph-disk zap " + data_disk + " " + journal_disk + " " + other_data_disk)

    #
    # Create an OSD and reuse an existing journal partition
    #
    def test_activate_reuse_journal(self):
        c = CephDisk()
        disks = c.unused_disks()
        data_disk = disks[0]
        journal_disk = disks[1]
        #
        # Create an OSD with a separated journal and destroy it.
        #
        osd_uuid = self.activate_separated_journal(data_disk, journal_disk)
        journal_partition = c.get_journal_partition(osd_uuid)
        journal_path = journal_partition['path']
        c.destroy_osd(osd_uuid)
        c.sh("ceph-disk zap " + data_disk)
        osd_uuid = str(uuid.uuid1())
        #
        # Create another OSD with the journal partition of the previous OSD
        #
        c.sh("ceph-disk prepare --osd-uuid " + osd_uuid +
             " " + data_disk + " " + journal_path)
        c.helper("pool_read_write 1") # 1 == pool size
        c.wait_for_osd_up(osd_uuid)
        device = json.loads(c.sh("ceph-disk list --format json " + data_disk))[0]
        assert len(device['partitions']) == 1
        data_partition = c.get_osd_partition(osd_uuid)
        assert data_partition['type'] == 'data'
        assert data_partition['state'] == 'active'
        journal_partition = c.get_journal_partition(osd_uuid)
        #
        # Verify the previous OSD partition has been reused
        #
        assert journal_partition['path'] == journal_path
        c.destroy_osd(osd_uuid)
        c.sh("ceph-disk zap " + data_disk + " " + journal_disk)

    def test_activate_multipath(self):
        c = CephDisk()
        if c.sh("lsb_release -si") != 'CentOS':
            pytest.skip("see issue https://bugs.launchpad.net/ubuntu/+source/multipath-tools/+bug/1488688")
        c.ensure_sd()
        #
        # Figure out the name of the multipath device
        #
        disk = c.unused_disks('sd.')[0]
        c.sh("mpathconf --enable || true")
        c.sh("multipath " + disk)
        holders = os.listdir("/sys/block/" + os.path.basename(disk) + "/holders")
        assert 1 == len(holders)
        name = open("/sys/block/" + holders[0] + "/dm/name").read()
        multipath = "/dev/mapper/" + name
        #
        # Prepare the multipath device
        #
        osd_uuid = str(uuid.uuid1())
        c.sh("ceph-disk zap " + multipath)
        c.sh("ceph-disk prepare --osd-uuid " + osd_uuid +
             " " + multipath)
        c.wait_for_osd_up(osd_uuid)
        device = json.loads(c.sh("ceph-disk list --format json " + multipath))[0]
        assert len(device['partitions']) == 2
        data_partition = c.get_osd_partition(osd_uuid)
        assert data_partition['type'] == 'data'
        assert data_partition['state'] == 'active'
        journal_partition = c.get_journal_partition(osd_uuid)
        assert journal_partition
        c.helper("pool_read_write")
        c.destroy_osd(osd_uuid)
        c.sh("ceph-disk zap " + multipath)
        c.sh("udevadm settle")
        c.sh("multipath -F")
        c.unload_scsi_debug()

class CephDiskTest(CephDisk):

    def main(self, argv):
        parser = argparse.ArgumentParser(
            'ceph-disk-test',
        )
        parser.add_argument(
            '-v', '--verbose',
            action='store_true', default=None,
            help='be more verbose',
        )
        parser.add_argument(
            '--destroy-osd',
            help='stop, umount and destroy',
        )
        args = parser.parse_args(argv)

        if args.verbose:
            logging.basicConfig(level=logging.DEBUG)

        if args.destroy_osd:
            dump = json.loads(CephDisk.sh("ceph osd dump -f json"))
            osd_uuid = None
            for osd in dump['osds']:
                if str(osd['osd']) == args.destroy_osd:
                    osd_uuid = osd['uuid']
            if osd_uuid:
                self.destroy_osd(osd_uuid)
            else:
                raise Exception("cannot find OSD " + args.destroy_osd +
                                " ceph osd dump -f json")
            return

if __name__ == '__main__':
    sys.exit(CephDiskTest().main(sys.argv[1:]))
