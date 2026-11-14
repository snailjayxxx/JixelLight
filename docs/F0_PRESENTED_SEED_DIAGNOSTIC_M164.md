# M164: retain both frames of the initial GPU seed mismatch

Accepted root b91de16 remains unchanged. M163/e460454 is held: its fresh Release
build and actual normal-window helper tests pass at Cocoa DPR1/2 (4/0 each),
and its first CPU pressure flow passes6787ms with38 normal captures, one exact
Qt control and22 original PNGs. The first GPU flow fails atphase50/899ms:
normal seed5d82db7697ca73f35ee0918ac70e467bcdc9c68e3d25f94d1f988768f99019f3
differs from immediate Qt control
828e12f2f11a17f30d55b33a9ed328eeb09e7e8ffc8a24c77015bdd5d99c187c.
The original exact-image failure remains a failure; the differing pair was
not saved by that version, so no unique cause or repair is claimed yet.

The held capsule has81 retained files/77,878,994 bytes plus a complete source
archive,19 runtime copies, five graphs and an inventory of all1400 build
files/376,308,005 bytes. Original objects/source/runtime remain in place.
Manifest SHA256:
a946fcff05678d0d9f8433cdd6ade0d5d9942feb63a7635c41dd4e821cabc97d.
An earlier external retention attempt assumed a successful validator's already
cleaned temporary work still existed; all partial copies and that interruption
remain. The completed capsule retains failed GPU work and all NAS evidence.
No product test is retried or reclassified by that retention recovery.

This diagnostic uses a new NAS source/head/physical subset build. It only
retains both first seed images and timing/read-only state after computing
the identical exact comparison. The fresh owned diagnostic prefix is supplied
by the external runner; quality85 PNGs cannot overwrite existing files and
survive successful temporary-work cleanup. These two diagnostic PNGs are
separate from the original22 mandatory captures and their bounded writer.
Original phase bodies, comparison/acceptance logic, deadline, backend,
validators and every image/RAW/shader algorithm remain unchanged.

Run the first actual GPU pressure case once, retain its original outcome and
both canonical frames, then inspect exact differing pixels. Diagnostic output
collection is not product/full107/CI/package acceptance. Do not rebuild either
held e460454 or accepted b91de16. Further corrections need another source/head
and physical build after the original diagnostic outcome is retained.
