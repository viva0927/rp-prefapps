uidir = $(datadir)/rp-prefapps

ui_in_files = 	rp_prefapps.ui \
				prefapps.conf \
				bluej.png \
				claws-mail.png \
				ctc.png \
				greenfoot.png \
				libreoffice-main.png \
				minecraft-pi.png \
				mu.png \
				node-red-icon.svg \
				orca.png \
				scratch.png \
				scratch2.png \
				scratch3.png \
				sense_emu_gui.png \
				smartsim.png \
				sonic-pi.png \
				thonny.png \
				vncviewer48x48.png \
				wolfram-mathematica.png
	
ui_DATA = $(ui_in_files)

desktopdir=$(datadir)/applications

desktop_in_files= \
	rp-prefapps.desktop.in \
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = $(ui_in_files) \
			$(desktop_in_files) \
			$(desktop_DATA) \
			$(NULL)
