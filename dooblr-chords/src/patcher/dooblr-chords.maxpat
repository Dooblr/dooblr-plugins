{
	"patcher": {
		"fileversion": 1,
		"appversion": {
			"major": 8,
			"minor": 5,
			"revision": 8,
			"architecture": "x64",
			"modernui": 1
		},
		"classnamespace": "box",
		"rect": [
			100,
			100,
			1280,
			640
		],
		"openrect": [
			0,
			0,
			0,
			169
		],
		"bglocked": 0,
		"openinpresentation": 1,
		"default_fontsize": 10,
		"default_fontface": 0,
		"default_fontname": "Arial",
		"gridonopen": 1,
		"gridsize": [
			8,
			8
		],
		"gridsnaponopen": 1,
		"objectsnaponopen": 1,
		"statusbarvisible": 2,
		"toolbarvisible": 1,
		"lefttoolbarpinned": 0,
		"toptoolbarpinned": 0,
		"righttoolbarpinned": 0,
		"bottomtoolbarpinned": 0,
		"toolbars_unpinned_last_save": 0,
		"tallnewobj": 0,
		"boxanimatetime": 200,
		"enablehscroll": 1,
		"enablevscroll": 1,
		"devicewidth": 640,
		"description": "Chord progression generator that writes clips into Session View.",
		"digest": "",
		"tags": "",
		"style": "",
		"subpatcher_template": "",
		"assistshowspatchername": 0,
		"boxes": [
			{
				"box": {
					"maxclass": "panel",
					"numinlets": 1,
					"numoutlets": 0,
					"mode": 0,
					"border": 0,
					"rounded": 0,
					"background": 1,
					"ignoreclick": 1,
					"bgcolor": [
						0.086,
						0.094,
						0.11,
						1
					],
					"bgfillcolor_type": "color",
					"bgfillcolor_color": [
						0.086,
						0.094,
						0.11,
						1
					],
					"id": "obj-1",
					"patching_rect": [
						0,
						0,
						640,
						169
					],
					"presentation": 1,
					"presentation_rect": [
						0,
						0,
						640,
						169
					]
				}
			},
			{
				"box": {
					"maxclass": "panel",
					"numinlets": 1,
					"numoutlets": 0,
					"mode": 0,
					"border": 0,
					"rounded": 0,
					"background": 1,
					"ignoreclick": 1,
					"bgcolor": [
						0.114,
						0.125,
						0.149,
						1
					],
					"bgfillcolor_type": "color",
					"bgfillcolor_color": [
						0.114,
						0.125,
						0.149,
						1
					],
					"id": "obj-2",
					"patching_rect": [
						0,
						0,
						146,
						169
					],
					"presentation": 1,
					"presentation_rect": [
						0,
						0,
						124,
						169
					]
				}
			},
			{
				"box": {
					"maxclass": "panel",
					"numinlets": 1,
					"numoutlets": 0,
					"mode": 0,
					"border": 0,
					"rounded": 0,
					"background": 1,
					"ignoreclick": 1,
					"bgcolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"bgfillcolor_type": "color",
					"bgfillcolor_color": [
						0.373,
						0.702,
						0.631,
						1
					],
					"id": "obj-3",
					"patching_rect": [
						10,
						12,
						3,
						32
					],
					"presentation": 1,
					"presentation_rect": [
						10,
						10,
						3,
						28
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "dooblr-chords",
					"fontname": "Arial Bold",
					"fontsize": 14,
					"textcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-4",
					"patching_rect": [
						18,
						10,
						124,
						22
					],
					"presentation": 1,
					"presentation_rect": [
						18,
						8,
						102,
						18
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "v0.4.0",
					"fontname": "Arial",
					"fontsize": 9,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-5",
					"patching_rect": [
						18,
						32,
						124,
						16
					],
					"presentation": 1,
					"presentation_rect": [
						18,
						26,
						54,
						14
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "generate",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 1,
					"mode": 0,
					"text": "Generate",
					"texton": "Generate",
					"fontname": "Arial Bold",
					"fontsize": 14,
					"activebgcolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"activebgoncolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activetextcolor": [
						0.086,
						0.094,
						0.11,
						1
					],
					"activetextoncolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"bgcolor": [
						0.373,
						0.702,
						0.631,
						0.38
					],
					"bgoncolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"textcolor": [
						0.086,
						0.094,
						0.11,
						0.38
					],
					"textoncolor": [
						0.902,
						0.91,
						0.922,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Generate",
							"parameter_shortname": "Generate",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_invisible": 2
						}
					},
					"annotation_name": "Generate",
					"annotation": "Generate makes a new progression with a new seed and writes it as a clip in the next empty slot on this track.",
					"id": "obj-6",
					"patching_rect": [
						10,
						56,
						126,
						24
					],
					"presentation": 1,
					"presentation_rect": [
						10,
						46,
						104,
						54
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "reroll",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 1,
					"mode": 0,
					"text": "Reroll",
					"texton": "Reroll",
					"fontname": "Arial Bold",
					"fontsize": 11,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activetextcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"activetextoncolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"textcolor": [
						0.902,
						0.91,
						0.922,
						0.38
					],
					"textoncolor": [
						0.902,
						0.91,
						0.922,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Reroll",
							"parameter_shortname": "Reroll",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_invisible": 2
						}
					},
					"annotation_name": "Reroll",
					"annotation": "Reroll keeps the locked chords, regenerates the others with a new seed and writes a clip.",
					"id": "obj-7",
					"patching_rect": [
						10,
						84,
						61,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						10,
						108,
						104,
						26
					]
				}
			},
			{
				"box": {
					"maxclass": "panel",
					"numinlets": 1,
					"numoutlets": 0,
					"mode": 0,
					"border": 0,
					"rounded": 0,
					"background": 1,
					"ignoreclick": 1,
					"bgcolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"bgfillcolor_type": "color",
					"bgfillcolor_color": [
						0.173,
						0.188,
						0.22,
						1
					],
					"id": "obj-12",
					"patching_rect": [
						146,
						0,
						1,
						169
					],
					"presentation": 1,
					"presentation_rect": [
						124,
						0,
						1,
						169
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "KEY",
					"fontname": "Arial Bold",
					"fontsize": 9,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-13",
					"patching_rect": [
						156,
						3,
						60,
						14
					],
					"presentation": 1,
					"presentation_rect": [
						136,
						4,
						50,
						14
					]
				}
			},
			{
				"box": {
					"maxclass": "live.menu",
					"varname": "root",
					"numinlets": 1,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						"float"
					],
					"parameter_enable": 1,
					"fontname": "Arial",
					"fontsize": 11,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"textcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"activetextcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"tricolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activetricolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Root",
							"parameter_shortname": "Root",
							"parameter_type": 2,
							"parameter_enum": [
								"C",
								"C#",
								"D",
								"Eb",
								"E",
								"F",
								"F#",
								"G",
								"Ab",
								"A",
								"Bb",
								"B"
							],
							"parameter_mmax": 11,
							"parameter_initial": [
								0
							],
							"parameter_initial_enable": 1,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Root",
					"annotation": "Root sets the key centre of the progression.",
					"id": "obj-16",
					"patching_rect": [
						158,
						31,
						38,
						16
					],
					"presentation": 1,
					"presentation_rect": [
						136,
						22,
						52,
						24
					]
				}
			},
			{
				"box": {
					"maxclass": "live.menu",
					"varname": "mode",
					"numinlets": 1,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						"float"
					],
					"parameter_enable": 1,
					"fontname": "Arial",
					"fontsize": 11,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"textcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"activetextcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"tricolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activetricolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Mode",
							"parameter_shortname": "Mode",
							"parameter_type": 2,
							"parameter_enum": [
								"Major",
								"Natural Minor",
								"Dorian",
								"Phrygian",
								"Lydian",
								"Mixolydian",
								"Harmonic Minor"
							],
							"parameter_mmax": 6,
							"parameter_initial": [
								1
							],
							"parameter_initial_enable": 1,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Mode",
					"annotation": "Mode sets the scale the chords are built from.",
					"id": "obj-17",
					"patching_rect": [
						202,
						31,
						86,
						16
					],
					"presentation": 1,
					"presentation_rect": [
						196,
						22,
						120,
						24
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "Length",
					"fontname": "Arial",
					"fontsize": 9,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-18",
					"patching_rect": [
						156,
						49,
						44,
						14
					],
					"presentation": 1,
					"presentation_rect": [
						136,
						52,
						52,
						14
					]
				}
			},
			{
				"box": {
					"maxclass": "live.numbox",
					"varname": "length",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						"float"
					],
					"parameter_enable": 1,
					"fontname": "Arial",
					"fontsize": 11,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"textcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"tricolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activetricolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"activeslidercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Length",
							"parameter_shortname": "Length",
							"parameter_type": 1,
							"parameter_mmin": 2,
							"parameter_mmax": 16,
							"parameter_initial": [
								4
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 0,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Length",
					"annotation": "Length is the number of chords in the progression.",
					"id": "obj-20",
					"patching_rect": [
						158,
						63,
						38,
						16
					],
					"presentation": 1,
					"presentation_rect": [
						136,
						66,
						52,
						24
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "Color",
					"fontname": "Arial",
					"fontsize": 9,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-23",
					"patching_rect": [
						294,
						3,
						90,
						14
					],
					"presentation": 1,
					"presentation_rect": [
						326,
						4,
						70,
						14
					],
					"varname": "color_label"
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "color",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						"float"
					],
					"parameter_enable": 1,
					"showname": 0,
					"shownumber": 1,
					"dialcolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"activedialcolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"needlecolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"activeneedlecolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"panelcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"textcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"fontname": "Arial",
					"fontsize": 10,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Color",
							"parameter_shortname": "Color",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								35
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Color",
					"annotation": "Color adds sevenths, ninths and other extensions. At 0 every chord is a plain triad.",
					"id": "obj-24",
					"patching_rect": [
						296,
						20,
						46,
						58
					],
					"presentation": 1,
					"presentation_rect": [
						326,
						18,
						70,
						76
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "chromatic",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						"float"
					],
					"parameter_enable": 1,
					"showname": 0,
					"shownumber": 1,
					"dialcolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"activedialcolor": [
						0.851,
						0.639,
						0.357,
						1
					],
					"needlecolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"activeneedlecolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"panelcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"textcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"fontname": "Arial",
					"fontsize": 10,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Chromatic",
							"parameter_shortname": "Chromatic",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								10
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Chromatic",
					"annotation": "Spice (the Chromatic control) adds borrowed chords, secondary dominants, tritone substitutions and the Neapolitan. At 0 every chord stays in the scale.",
					"id": "obj-25",
					"patching_rect": [
						346,
						20,
						46,
						58
					],
					"presentation": 1,
					"presentation_rect": [
						404,
						18,
						70,
						76
					]
				}
			},
			{
				"box": {
					"maxclass": "jsui",
					"filename": "dooblr_blocks.js",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"parameter_enable": 0,
					"border": 0,
					"annotation_name": "Progression",
					"annotation": "Click a chord to lock it. Locked chords stay the same when you press Reroll.",
					"id": "obj-38",
					"patching_rect": [
						136,
						99,
						496,
						66
					],
					"presentation": 1,
					"presentation_rect": [
						136,
						99,
						496,
						66
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "live.thisdevice",
					"numinlets": 1,
					"numoutlets": 3,
					"outlettype": [
						"bang",
						"int",
						"int"
					],
					"id": "obj-39",
					"patching_rect": [
						800,
						20,
						90,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "init",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-40",
					"patching_rect": [
						800,
						52,
						34,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "js dooblr_chords.js",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"saved_object_attributes": {
						"filename": "dooblr_chords.js",
						"parameter_enable": 0
					},
					"id": "obj-41",
					"patching_rect": [
						800,
						420,
						200,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param root",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-42",
					"patching_rect": [
						800,
						90,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param mode",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-43",
					"patching_rect": [
						950,
						90,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param length",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-44",
					"patching_rect": [
						1100,
						90,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param duration",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-45",
					"patching_rect": [
						1250,
						90,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param color",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-46",
					"patching_rect": [
						800,
						120,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param chromatic",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-47",
					"patching_rect": [
						950,
						120,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param register",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-48",
					"patching_rect": [
						1100,
						120,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param spread",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-49",
					"patching_rect": [
						1250,
						120,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param bass",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-50",
					"patching_rect": [
						800,
						150,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param velocity",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-51",
					"patching_rect": [
						950,
						150,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param humanize",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-52",
					"patching_rect": [
						1100,
						150,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param seed",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-53",
					"patching_rect": [
						1250,
						150,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						""
					],
					"id": "obj-54",
					"patching_rect": [
						800,
						200,
						80,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "generate",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-55",
					"patching_rect": [
						800,
						232,
						70,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						""
					],
					"id": "obj-56",
					"patching_rect": [
						950,
						200,
						80,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "reroll",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-57",
					"patching_rect": [
						950,
						232,
						70,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						""
					],
					"id": "obj-58",
					"patching_rect": [
						1100,
						200,
						80,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "write",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-59",
					"patching_rect": [
						1100,
						232,
						70,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "midiin",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						"int"
					],
					"id": "obj-60",
					"patching_rect": [
						1100,
						20,
						50,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "midiout",
					"numinlets": 1,
					"numoutlets": 0,
					"id": "obj-61",
					"patching_rect": [
						1100,
						60,
						55,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"text": "Controls send \"param <name> <value>\" to the js. The drop zone sends \"dropfile <path>\"; USE CLIP and CLEAR send \"useclip\" and \"clearsource\". ADVANCED … and SOURCE … send \"openpanel <name> <0|1>\"; the close buttons send \"closepanel\". The js hides overlay objects with the hidden attribute, sends \"setstate\" to the display and \"set <seed>\" back to the Seed box, and sets srcinfo, srcbadge, root, mode and length by varname.",
					"numinlets": 1,
					"numoutlets": 0,
					"fontsize": 10,
					"id": "obj-62",
					"patching_rect": [
						800,
						460,
						520,
						48
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend dropfile",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-70",
					"patching_rect": [
						1250,
						200,
						110,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						""
					],
					"id": "obj-71",
					"patching_rect": [
						1400,
						200,
						80,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "useclip",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-72",
					"patching_rect": [
						1400,
						232,
						60,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						""
					],
					"id": "obj-73",
					"patching_rect": [
						1500,
						200,
						80,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "clearsource",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-74",
					"patching_rect": [
						1500,
						232,
						80,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param kind",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-75",
					"patching_rect": [
						1400,
						150,
						140,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "Spice",
					"fontname": "Arial",
					"fontsize": 9,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-76",
					"patching_rect": [
						404,
						200,
						70,
						14
					],
					"presentation": 1,
					"presentation_rect": [
						404,
						4,
						70,
						14
					],
					"varname": "spice_label"
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend openpanel advanced",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-84",
					"patching_rect": [
						20,
						252,
						180,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend openpanel source",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-85",
					"patching_rect": [
						110,
						252,
						180,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						""
					],
					"id": "obj-86",
					"patching_rect": [
						200,
						252,
						80,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "closepanel",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"id": "obj-87",
					"patching_rect": [
						200,
						284,
						80,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "panel",
					"numinlets": 1,
					"numoutlets": 0,
					"mode": 0,
					"border": 0,
					"rounded": 0,
					"background": 0,
					"ignoreclick": 0,
					"bgcolor": [
						0.114,
						0.125,
						0.149,
						1
					],
					"bgfillcolor_type": "color",
					"bgfillcolor_color": [
						0.114,
						0.125,
						0.149,
						1
					],
					"id": "obj-78",
					"patching_rect": [
						128,
						260,
						200,
						40
					],
					"presentation": 1,
					"presentation_rect": [
						128,
						0,
						512,
						169
					],
					"varname": "adv_bg",
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "panel",
					"numinlets": 1,
					"numoutlets": 0,
					"mode": 0,
					"border": 0,
					"rounded": 0,
					"background": 0,
					"ignoreclick": 0,
					"bgcolor": [
						0.114,
						0.125,
						0.149,
						1
					],
					"bgfillcolor_type": "color",
					"bgfillcolor_color": [
						0.114,
						0.125,
						0.149,
						1
					],
					"id": "obj-79",
					"patching_rect": [
						348,
						260,
						200,
						40
					],
					"presentation": 1,
					"presentation_rect": [
						128,
						0,
						512,
						169
					],
					"varname": "src_bg",
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "write",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 1,
					"mode": 0,
					"text": "Write",
					"texton": "Write",
					"fontname": "Arial Bold",
					"fontsize": 11,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activetextcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"activetextoncolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"textcolor": [
						0.902,
						0.91,
						0.922,
						0.38
					],
					"textoncolor": [
						0.902,
						0.91,
						0.922,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Write",
							"parameter_shortname": "Write",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_invisible": 2
						}
					},
					"annotation_name": "Write",
					"annotation": "Write puts the progression shown below into the next empty clip slot without changing it.",
					"id": "obj-8",
					"patching_rect": [
						75,
						84,
						61,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						404,
						50,
						64,
						24
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "Seed",
					"fontname": "Arial",
					"fontsize": 9,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-9",
					"patching_rect": [
						10,
						113,
						34,
						16
					],
					"presentation": 1,
					"presentation_rect": [
						304,
						36,
						40,
						14
					],
					"varname": "adv_seed_label",
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.numbox",
					"varname": "seed",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						"float"
					],
					"parameter_enable": 1,
					"fontname": "Arial",
					"fontsize": 11,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"tricolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activetricolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activeslidercolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Seed",
							"parameter_shortname": "Seed",
							"parameter_type": 1,
							"parameter_mmin": 1,
							"parameter_mmax": 99999,
							"parameter_initial": [
								1
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 0,
							"parameter_invisible": 1
						}
					},
					"annotation_name": "Seed",
					"annotation": "The seed makes a progression repeatable. The same seed and settings always give the same chords.",
					"id": "obj-10",
					"patching_rect": [
						46,
						112,
						90,
						18
					],
					"presentation": 1,
					"presentation_rect": [
						304,
						50,
						88,
						24
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "Bars per Chord",
					"fontname": "Arial",
					"fontsize": 9,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-19",
					"patching_rect": [
						200,
						49,
						90,
						14
					],
					"presentation": 1,
					"presentation_rect": [
						140,
						36,
						100,
						14
					],
					"varname": "adv_bars_label",
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.tab",
					"varname": "duration",
					"numinlets": 1,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						"float"
					],
					"parameter_enable": 1,
					"num_lines_patching": 1,
					"num_lines_presentation": 1,
					"fontname": "Arial",
					"fontsize": 11,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"activetextcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"activetextoncolor": [
						0.086,
						0.094,
						0.11,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.373,
						0.702,
						0.631,
						0.38
					],
					"textcolor": [
						0.902,
						0.91,
						0.922,
						0.38
					],
					"textoncolor": [
						0.086,
						0.094,
						0.11,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Duration",
							"parameter_shortname": "Duration",
							"parameter_type": 2,
							"parameter_enum": [
								"1/2",
								"1",
								"2"
							],
							"parameter_mmax": 2,
							"parameter_initial": [
								1
							],
							"parameter_initial_enable": 1,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Bars per Chord",
					"annotation": "Bars per Chord sets how long each chord lasts: half a bar, one bar or two bars.",
					"id": "obj-21",
					"patching_rect": [
						202,
						63,
						86,
						16
					],
					"presentation": 1,
					"presentation_rect": [
						140,
						50,
						148,
						24
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "ADVANCED",
					"fontname": "Arial Bold",
					"fontsize": 11,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-27",
					"patching_rect": [
						398,
						3,
						90,
						14
					],
					"presentation": 1,
					"presentation_rect": [
						140,
						8,
						200,
						16
					],
					"varname": "adv_title",
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "register",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						"float"
					],
					"parameter_enable": 1,
					"showname": 1,
					"shownumber": 1,
					"dialcolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"activedialcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"needlecolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activeneedlecolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"panelcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"fontname": "Arial",
					"fontsize": 10,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Register",
							"parameter_shortname": "Register",
							"parameter_type": 1,
							"parameter_mmin": 36,
							"parameter_mmax": 72,
							"parameter_initial": [
								60
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 8,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Register",
					"annotation": "Register sets the lowest note of the upper voices.",
					"id": "obj-28",
					"patching_rect": [
						400,
						20,
						46,
						58
					],
					"presentation": 1,
					"presentation_rect": [
						140,
						84,
						64,
						72
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "spread",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						"float"
					],
					"parameter_enable": 1,
					"showname": 1,
					"shownumber": 1,
					"dialcolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"activedialcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"needlecolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activeneedlecolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"panelcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"fontname": "Arial",
					"fontsize": 10,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Spread",
							"parameter_shortname": "Spread",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								25
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Spread",
					"annotation": "Spread opens the voicing from close position to drop-2 to wide open.",
					"id": "obj-29",
					"patching_rect": [
						450,
						20,
						46,
						58
					],
					"presentation": 1,
					"presentation_rect": [
						212,
						84,
						64,
						72
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "Bass",
					"fontname": "Arial",
					"fontsize": 9,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-30",
					"patching_rect": [
						500,
						20,
						44,
						14
					],
					"presentation": 1,
					"presentation_rect": [
						432,
						84,
						48,
						14
					],
					"varname": "bass_label",
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "bass",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 1,
					"mode": 1,
					"text": "Off",
					"texton": "On",
					"fontname": "Arial",
					"fontsize": 10,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"activetextcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activetextoncolor": [
						0.086,
						0.094,
						0.11,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.373,
						0.702,
						0.631,
						0.38
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						0.38
					],
					"textoncolor": [
						0.086,
						0.094,
						0.11,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Bass",
							"parameter_shortname": "Bass",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_initial": [
								1
							],
							"parameter_initial_enable": 1,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Bass",
					"annotation": "Bass adds a separate bass note an octave or more below the chord.",
					"id": "obj-31",
					"patching_rect": [
						500,
						36,
						40,
						18
					],
					"presentation": 1,
					"presentation_rect": [
						432,
						100,
						48,
						22
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "velocity",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						"float"
					],
					"parameter_enable": 1,
					"showname": 1,
					"shownumber": 1,
					"dialcolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"activedialcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"needlecolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activeneedlecolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"panelcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"fontname": "Arial",
					"fontsize": 10,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Velocity",
							"parameter_shortname": "Velocity",
							"parameter_type": 1,
							"parameter_mmin": 1,
							"parameter_mmax": 127,
							"parameter_initial": [
								100
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 0,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Velocity",
					"annotation": "Velocity sets how hard every note is played.",
					"id": "obj-34",
					"patching_rect": [
						552,
						20,
						46,
						58
					],
					"presentation": 1,
					"presentation_rect": [
						284,
						84,
						64,
						72
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "humanize",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						"float"
					],
					"parameter_enable": 1,
					"showname": 1,
					"shownumber": 1,
					"dialcolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"activedialcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"needlecolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activeneedlecolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"panelcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"fontname": "Arial",
					"fontsize": 10,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Humanize",
							"parameter_shortname": "Humanize",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								0
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Humanize",
					"annotation": "Humanize nudges note timing and velocity a little. At 0 the notes are exactly on the grid.",
					"id": "obj-35",
					"patching_rect": [
						604,
						20,
						50,
						58
					],
					"presentation": 1,
					"presentation_rect": [
						356,
						84,
						64,
						72
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "SOURCE",
					"fontname": "Arial Bold",
					"fontsize": 11,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-64",
					"patching_rect": [
						766,
						3,
						90,
						14
					],
					"presentation": 1,
					"presentation_rect": [
						140,
						8,
						200,
						16
					],
					"varname": "src_title",
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.drop",
					"varname": "srcdrop",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 1,
					"legend": "Drop a .mid file",
					"fontname": "Arial",
					"fontsize": 10,
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"bordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Source File",
							"parameter_shortname": "Source",
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Source File",
					"annotation": "Drop a MIDI file (.mid) here to use its chords as the source. VAR and B then write a variation or a B section of it. The file path is saved with the Set.",
					"id": "obj-65",
					"patching_rect": [
						768,
						19,
						128,
						44
					],
					"presentation": 1,
					"presentation_rect": [
						140,
						40,
						248,
						52
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "useclip",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 1,
					"mode": 0,
					"text": "USE CLIP",
					"texton": "USE CLIP",
					"fontname": "Arial Bold",
					"fontsize": 11,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activetextcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"activetextoncolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"textcolor": [
						0.902,
						0.91,
						0.922,
						0.38
					],
					"textoncolor": [
						0.902,
						0.91,
						0.922,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Use Clip",
							"parameter_shortname": "Use Clip",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_invisible": 2
						}
					},
					"annotation_name": "Use Clip",
					"annotation": "USE CLIP reads the MIDI clip shown in the Detail View (or the selected clip) as the source.",
					"id": "obj-66",
					"patching_rect": [
						768,
						67,
						62,
						18
					],
					"presentation": 1,
					"presentation_rect": [
						400,
						40,
						92,
						24
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "clearsource",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 1,
					"mode": 0,
					"text": "CLEAR",
					"texton": "CLEAR",
					"fontname": "Arial",
					"fontsize": 9,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activetextcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activetextoncolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						0.38
					],
					"textoncolor": [
						0.902,
						0.91,
						0.922,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Clear Source",
							"parameter_shortname": "Clear",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_invisible": 2
						}
					},
					"annotation_name": "Clear Source",
					"annotation": "CLEAR forgets the source, so Generate makes fresh progressions again.",
					"id": "obj-67",
					"patching_rect": [
						834,
						67,
						62,
						18
					],
					"presentation": 1,
					"presentation_rect": [
						500,
						40,
						52,
						24
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.tab",
					"varname": "kind",
					"numinlets": 1,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						"float"
					],
					"parameter_enable": 1,
					"num_lines_patching": 1,
					"num_lines_presentation": 1,
					"fontname": "Arial Bold",
					"fontsize": 11,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"activetextcolor": [
						0.902,
						0.91,
						0.922,
						1
					],
					"activetextoncolor": [
						0.086,
						0.094,
						0.11,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.373,
						0.702,
						0.631,
						0.38
					],
					"textcolor": [
						0.902,
						0.91,
						0.922,
						0.38
					],
					"textoncolor": [
						0.086,
						0.094,
						0.11,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Source Kind",
							"parameter_shortname": "Kind",
							"parameter_type": 2,
							"parameter_enum": [
								"VAR",
								"B"
							],
							"parameter_mmax": 1,
							"parameter_initial": [
								0
							],
							"parameter_initial_enable": 1,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "VAR / B",
					"annotation": "VAR writes a variation of the source: same key, length and rhythm, some chords swapped. B writes a contrasting B section that leads back to the source.",
					"id": "obj-68",
					"patching_rect": [
						768,
						90,
						128,
						18
					],
					"presentation": 1,
					"presentation_rect": [
						140,
						100,
						248,
						28
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "Drop a .mid file or press USE CLIP.",
					"fontname": "Arial",
					"fontsize": 9,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-69",
					"patching_rect": [
						766,
						112,
						132,
						52
					],
					"presentation": 1,
					"presentation_rect": [
						400,
						72,
						200,
						80
					],
					"varname": "srcinfo",
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "adv_close",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 0,
					"mode": 0,
					"text": "×",
					"texton": "×",
					"fontname": "Arial Bold",
					"fontsize": 14,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activetextcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activetextoncolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						0.38
					],
					"textoncolor": [
						0.541,
						0.569,
						0.612,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"annotation_name": "Close Advanced",
					"annotation": "Closes the Advanced panel.",
					"id": "obj-82",
					"patching_rect": [
						200,
						220,
						22,
						22
					],
					"presentation": 1,
					"presentation_rect": [
						456,
						6,
						22,
						22
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "src_close",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 0,
					"mode": 0,
					"text": "×",
					"texton": "×",
					"fontname": "Arial Bold",
					"fontsize": 14,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activetextcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activetextoncolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						0.38
					],
					"textoncolor": [
						0.541,
						0.569,
						0.612,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"annotation_name": "Close Source",
					"annotation": "Closes the Source panel.",
					"id": "obj-83",
					"patching_rect": [
						230,
						220,
						22,
						22
					],
					"presentation": 1,
					"presentation_rect": [
						456,
						6,
						22,
						22
					],
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "",
					"fontname": "Arial Bold",
					"fontsize": 9,
					"textcolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-77",
					"patching_rect": [
						8,
						200,
						108,
						22
					],
					"presentation": 1,
					"presentation_rect": [
						8,
						140,
						108,
						22
					],
					"varname": "srcbadge",
					"hidden": 1
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "open_advanced",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 0,
					"mode": 1,
					"text": "ADVANCED …",
					"texton": "ADVANCED …",
					"fontname": "Arial",
					"fontsize": 8.5,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"activetextcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activetextoncolor": [
						0.086,
						0.094,
						0.11,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.373,
						0.702,
						0.631,
						0.38
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						0.38
					],
					"textoncolor": [
						0.086,
						0.094,
						0.11,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"annotation_name": "Advanced",
					"annotation": "Opens the Advanced panel: Bars per Chord, voicing, velocity, humanize, seed and Write.",
					"id": "obj-80",
					"patching_rect": [
						20,
						220,
						80,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						486,
						6,
						70,
						20
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "open_source",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 0,
					"mode": 1,
					"text": "SOURCE …",
					"texton": "SOURCE …",
					"fontname": "Arial",
					"fontsize": 8.5,
					"activebgcolor": [
						0.145,
						0.161,
						0.192,
						1
					],
					"activebgoncolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"activetextcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"activetextoncolor": [
						0.086,
						0.094,
						0.11,
						1
					],
					"bgcolor": [
						0.145,
						0.161,
						0.192,
						0.38
					],
					"bgoncolor": [
						0.373,
						0.702,
						0.631,
						0.38
					],
					"textcolor": [
						0.541,
						0.569,
						0.612,
						0.38
					],
					"textoncolor": [
						0.086,
						0.094,
						0.11,
						0.38
					],
					"bordercolor": [
						0.173,
						0.188,
						0.22,
						1
					],
					"focusbordercolor": [
						0.373,
						0.702,
						0.631,
						1
					],
					"annotation_name": "Source",
					"annotation": "Opens the Source panel: drop a .mid file, use the selected clip, or choose a variation or B section.",
					"id": "obj-81",
					"patching_rect": [
						110,
						220,
						80,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						560,
						6,
						64,
						20
					]
				}
			}
		],
		"lines": [
			{
				"patchline": {
					"source": [
						"obj-39",
						0
					],
					"destination": [
						"obj-40",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-40",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-16",
						0
					],
					"destination": [
						"obj-42",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-42",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-17",
						0
					],
					"destination": [
						"obj-43",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-43",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-20",
						0
					],
					"destination": [
						"obj-44",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-44",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-21",
						0
					],
					"destination": [
						"obj-45",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-45",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-24",
						0
					],
					"destination": [
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-46",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-25",
						0
					],
					"destination": [
						"obj-47",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-47",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-28",
						0
					],
					"destination": [
						"obj-48",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-48",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-29",
						0
					],
					"destination": [
						"obj-49",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-49",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-31",
						0
					],
					"destination": [
						"obj-50",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-50",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-34",
						0
					],
					"destination": [
						"obj-51",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-51",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-35",
						0
					],
					"destination": [
						"obj-52",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-52",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-10",
						0
					],
					"destination": [
						"obj-53",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-53",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-6",
						0
					],
					"destination": [
						"obj-54",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-54",
						0
					],
					"destination": [
						"obj-55",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-54",
						1
					],
					"destination": [
						"obj-55",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-55",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-7",
						0
					],
					"destination": [
						"obj-56",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-56",
						0
					],
					"destination": [
						"obj-57",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-56",
						1
					],
					"destination": [
						"obj-57",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-57",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-8",
						0
					],
					"destination": [
						"obj-58",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-58",
						0
					],
					"destination": [
						"obj-59",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-58",
						1
					],
					"destination": [
						"obj-59",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-59",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-38",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-41",
						0
					],
					"destination": [
						"obj-38",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-41",
						1
					],
					"destination": [
						"obj-10",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-60",
						0
					],
					"destination": [
						"obj-61",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-65",
						0
					],
					"destination": [
						"obj-70",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-70",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-66",
						0
					],
					"destination": [
						"obj-71",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-71",
						0
					],
					"destination": [
						"obj-72",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-71",
						1
					],
					"destination": [
						"obj-72",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-67",
						0
					],
					"destination": [
						"obj-73",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-73",
						0
					],
					"destination": [
						"obj-74",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-73",
						1
					],
					"destination": [
						"obj-74",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-74",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-68",
						0
					],
					"destination": [
						"obj-75",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-80",
						0
					],
					"destination": [
						"obj-84",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-84",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-81",
						0
					],
					"destination": [
						"obj-85",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-85",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-82",
						0
					],
					"destination": [
						"obj-86",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-83",
						0
					],
					"destination": [
						"obj-86",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-86",
						0
					],
					"destination": [
						"obj-87",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-86",
						1
					],
					"destination": [
						"obj-87",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-87",
						0
					],
					"destination": [
						"obj-41",
						0
					]
				}
			}
		],
		"dependency_cache": [],
		"latency": 0,
		"is_mpe": 0,
		"minimum_live_version": "",
		"minimum_max_version": "",
		"platform_compatibility": 0,
		"autosave": 0
	}
}
