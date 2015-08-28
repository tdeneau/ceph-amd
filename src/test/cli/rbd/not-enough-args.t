  $ rbd info
  rbd: image name was not specified
  [1]
  $ rbd create
  rbd: image name was not specified
  [1]
  $ rbd clone
  rbd: image name was not specified
  [1]
  $ rbd clone foo
  rbd: snap name was not specified
  [1]
  $ rbd clone foo@snap
  rbd: destination image name was not specified
  [1]
  $ rbd clone foo bar
  rbd: snap name was not specified
  [1]
  $ rbd clone foo bar@snap
  rbd: snap name was not specified
  [1]
  $ rbd children
  rbd: image name was not specified
  [1]
  $ rbd children foo
  rbd: snap name was not specified
  [1]
  $ rbd flatten
  rbd: image name was not specified
  [1]
  $ rbd resize
  rbd: image name was not specified
  [1]
  $ rbd rm
  rbd: image name was not specified
  [1]
  $ rbd export
  rbd: image name was not specified
  [1]
  $ rbd import
  rbd: path was not specified
  [1]
  $ rbd diff
  rbd: image name was not specified
  [1]
  $ rbd export-diff
  rbd: image name was not specified
  [1]
  $ rbd export-diff foo
  rbd: path was not specified
  [1]
  $ rbd export-diff foo@snap
  rbd: path was not specified
  [1]
  $ rbd merge-diff
  rbd: first diff was not specified
  [1]
  $ rbd merge-diff /tmp/diff1
  rbd: second diff was not specified
  [1]
  $ rbd merge-diff /tmp/diff1 /tmp/diff2
  rbd: path was not specified
  [1]
  $ rbd import-diff
  rbd: path was not specified
  [1]
  $ rbd import-diff /tmp/diff
  rbd: image name was not specified
  [1]
  $ rbd cp
  rbd: image name was not specified
  [1]
  $ rbd cp foo
  rbd: destination image name was not specified
  [1]
  $ rbd cp foo@snap
  rbd: destination image name was not specified
  [1]
  $ rbd mv
  rbd: image name was not specified
  [1]
  $ rbd mv foo
  rbd: destination image name was not specified
  [1]
  $ rbd image-meta list
  rbd: image name was not specified
  [1]
  $ rbd image-meta get
  rbd: image name was not specified
  [1]
  $ rbd image-meta get foo
  rbd: metadata key was not specified
  [1]
  $ rbd image-meta set
  rbd: image name was not specified
  [1]
  $ rbd image-meta set foo
  rbd: metadata key was not specified
  [1]
  $ rbd image-meta set foo key
  rbd: metadata value was not specified
  [1]
  $ rbd image-meta remove
  rbd: image name was not specified
  [1]
  $ rbd image-meta remove foo
  rbd: metadata key was not specified
  [1]
  $ rbd object-map rebuild
  rbd: image name was not specified
  [1]
  $ rbd snap ls
  rbd: image name was not specified
  [1]
  $ rbd snap create
  rbd: image name was not specified
  [1]
  $ rbd snap create foo
  rbd: snap name was not specified
  [1]
  $ rbd snap rollback
  rbd: image name was not specified
  [1]
  $ rbd snap rollback foo
  rbd: snap name was not specified
  [1]
  $ rbd snap rm
  rbd: image name was not specified
  [1]
  $ rbd snap rm foo
  rbd: snap name was not specified
  [1]
  $ rbd snap purge
  rbd: image name was not specified
  [1]
  $ rbd snap protect
  rbd: image name was not specified
  [1]
  $ rbd snap protect foo
  rbd: snap name was not specified
  [1]
  $ rbd snap unprotect
  rbd: image name was not specified
  [1]
  $ rbd snap unprotect foo
  rbd: snap name was not specified
  [1]
  $ rbd watch
  rbd: image name was not specified
  [1]
  $ rbd status
  rbd: image name was not specified
  [1]
  $ rbd map
  rbd: image name was not specified
  [1]
  $ rbd unmap
  rbd: unmap requires either image name or device path
  [1]
  $ rbd feature disable
  rbd: image name was not specified
  [1]
  $ rbd feature disable foo
  rbd: at least one feature name must be specified
  [1]
  $ rbd feature enable
  rbd: image name was not specified
  [1]
  $ rbd feature enable foo
  rbd: at least one feature name must be specified
  [1]
  $ rbd lock list
  rbd: image name was not specified
  [1]
  $ rbd lock add
  rbd: image name was not specified
  [1]
  $ rbd lock add foo
  rbd: lock id was not specified
  [1]
  $ rbd lock remove
  rbd: image name was not specified
  [1]
  $ rbd lock remove foo
  rbd: lock id was not specified
  [1]
  $ rbd lock remove foo id
  rbd: locker was not specified
  [1]
  $ rbd bench-write
  rbd: image name was not specified
  [1]
