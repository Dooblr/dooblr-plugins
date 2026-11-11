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
			1400,
			720
		],
		"openrect": [
			0,
			0,
			0,
			252
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
		"devicewidth": 860,
		"description": "Tightens MIDI clips and writes musical variations into Session View.",
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
						860,
						252
					],
					"presentation": 1,
					"presentation_rect": [
						0,
						0,
						860,
						252
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
						136,
						252
					],
					"presentation": 1,
					"presentation_rect": [
						0,
						0,
						136,
						252
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
						12,
						10,
						3,
						30
					],
					"presentation": 1,
					"presentation_rect": [
						12,
						10,
						3,
						30
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "dooblr-titan",
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
						20,
						8,
						112,
						18
					],
					"presentation": 1,
					"presentation_rect": [
						20,
						8,
						112,
						18
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "v0.1.0",
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
						20,
						26,
						50,
						14
					],
					"presentation": 1,
					"presentation_rect": [
						20,
						26,
						50,
						14
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "tighten",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 1,
					"mode": 0,
					"text": "Tighten",
					"texton": "Tighten",
					"fontname": "Arial Bold",
					"fontsize": 13,
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
							"parameter_longname": "Tighten",
							"parameter_shortname": "Tighten",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_invisible": 2
						}
					},
					"annotation_name": "Tighten",
					"annotation": "Tighten writes an improved copy of the loaded clip into the next empty slot.",
					"id": "obj-6",
					"patching_rect": [
						12,
						46,
						112,
						34
					],
					"presentation": 1,
					"presentation_rect": [
						12,
						46,
						112,
						34
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
					"annotation": "Generate builds four variations of the loaded clip.",
					"id": "obj-7",
					"patching_rect": [
						12,
						84,
						112,
						26
					],
					"presentation": 1,
					"presentation_rect": [
						12,
						84,
						112,
						26
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "another",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 1,
					"mode": 0,
					"text": "Another",
					"texton": "Another",
					"fontname": "Arial",
					"fontsize": 10,
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
							"parameter_longname": "Another",
							"parameter_shortname": "Another",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_invisible": 2
						}
					},
					"annotation_name": "Another",
					"annotation": "Another picks a new seed and rebuilds the four variations.",
					"id": "obj-8",
					"patching_rect": [
						12,
						114,
						68,
						22
					],
					"presentation": 1,
					"presentation_rect": [
						12,
						114,
						68,
						22
					]
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
					"annotation": "Write puts the selected variation into the next empty clip slot.",
					"id": "obj-9",
					"patching_rect": [
						84,
						114,
						40,
						22
					],
					"presentation": 1,
					"presentation_rect": [
						84,
						114,
						40,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "savemidi",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": [
						"",
						""
					],
					"parameter_enable": 1,
					"mode": 0,
					"text": "Save MIDI",
					"texton": "Save MIDI",
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
							"parameter_longname": "Save MIDI",
							"parameter_shortname": "Save MIDI",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_invisible": 2
						}
					},
					"annotation_name": "Save MIDI",
					"annotation": "Save MIDI writes the last Tighten or Write result as a .mid file.",
					"id": "obj-10",
					"patching_rect": [
						12,
						140,
						112,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						12,
						140,
						112,
						20
					]
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
					"id": "obj-11",
					"patching_rect": [
						12,
						172,
						28,
						14
					],
					"presentation": 1,
					"presentation_rect": [
						12,
						172,
						28,
						14
					]
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
					"fontsize": 9,
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
					"annotation": "The seed makes a variation repeatable.",
					"id": "obj-12",
					"patching_rect": [
						42,
						170,
						82,
						18
					],
					"presentation": 1,
					"presentation_rect": [
						42,
						170,
						82,
						18
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "TIGHTEN",
					"fontname": "Arial Bold",
					"fontsize": 10,
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
						146,
						6,
						80,
						12
					],
					"presentation": 1,
					"presentation_rect": [
						146,
						6,
						80,
						12
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "qstrength",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Q Str",
							"parameter_shortname": "Q Str",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								100
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Q Str",
					"annotation": "Quantize strength. 0 leaves timing, 100 snaps to the grid.",
					"id": "obj-14",
					"patching_rect": [
						146,
						18,
						50,
						62
					],
					"presentation": 1,
					"presentation_rect": [
						146,
						18,
						50,
						62
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "swing",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Swing",
							"parameter_shortname": "Swing",
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
					"annotation_name": "Swing",
					"annotation": "Swing delays offbeats. 100 puts the offbeat on the last triplet of the pair.",
					"id": "obj-15",
					"patching_rect": [
						198,
						18,
						50,
						62
					],
					"presentation": 1,
					"presentation_rect": [
						198,
						18,
						50,
						62
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "minlen",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "MinLen",
							"parameter_shortname": "MinLen",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								8
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "MinLen",
					"annotation": "Drop notes shorter than this (0 to a sixteenth).",
					"id": "obj-16",
					"patching_rect": [
						250,
						18,
						50,
						62
					],
					"presentation": 1,
					"presentation_rect": [
						250,
						18,
						50,
						62
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "minvel",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "MinVel",
							"parameter_shortname": "MinVel",
							"parameter_type": 1,
							"parameter_mmin": 1,
							"parameter_mmax": 127,
							"parameter_initial": [
								8
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 0,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "MinVel",
					"annotation": "Drop notes quieter than this velocity.",
					"id": "obj-17",
					"patching_rect": [
						302,
						18,
						50,
						62
					],
					"presentation": 1,
					"presentation_rect": [
						302,
						18,
						50,
						62
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "gapclose",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Gap",
							"parameter_shortname": "Gap",
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
					"annotation_name": "Gap",
					"annotation": "Close same-pitch holes up to this size. Needs Legato on.",
					"id": "obj-18",
					"patching_rect": [
						354,
						18,
						50,
						62
					],
					"presentation": 1,
					"presentation_rect": [
						354,
						18,
						50,
						62
					]
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Human",
							"parameter_shortname": "Human",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								6
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Human",
					"annotation": "Seeded velocity jitter after compression.",
					"id": "obj-19",
					"patching_rect": [
						406,
						18,
						50,
						62
					],
					"presentation": 1,
					"presentation_rect": [
						406,
						18,
						50,
						62
					]
				}
			},
			{
				"box": {
					"maxclass": "live.menu",
					"varname": "grid",
					"numinlets": 1,
					"numoutlets": 3,
					"outlettype": [
						"",
						"",
						"float"
					],
					"parameter_enable": 1,
					"fontname": "Arial",
					"fontsize": 10,
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
							"parameter_longname": "Grid",
							"parameter_shortname": "Grid",
							"parameter_type": 2,
							"parameter_enum": [
								"1/4",
								"1/8",
								"1/8T",
								"1/16",
								"1/16T",
								"1/32",
								"1/32T"
							],
							"parameter_mmax": 6,
							"parameter_initial": [
								3
							],
							"parameter_initial_enable": 1,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Grid",
					"annotation": "Quantize grid, including triplet values.",
					"id": "obj-20",
					"patching_rect": [
						146,
						84,
						78,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						146,
						84,
						78,
						20
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "groove",
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
					"fontsize": 9,
					"activebgcolor": [
						0.373,
						0.702,
						0.631,
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
						0.373,
						0.702,
						0.631,
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
							"parameter_longname": "Groove",
							"parameter_shortname": "Groove",
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
					"annotation_name": "Groove",
					"annotation": "Keep the clip's consistent micro-timing while pulling outliers onto it.",
					"id": "obj-21",
					"patching_rect": [
						228,
						84,
						44,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						228,
						84,
						44,
						20
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "flams",
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
					"fontsize": 9,
					"activebgcolor": [
						0.373,
						0.702,
						0.631,
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
						0.373,
						0.702,
						0.631,
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
							"parameter_longname": "Flams",
							"parameter_shortname": "Flams",
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
					"annotation_name": "Flams",
					"annotation": "Merge same-pitch hits that start almost together; keep the louder one.",
					"id": "obj-22",
					"patching_rect": [
						276,
						84,
						44,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						276,
						84,
						44,
						20
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "legato",
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
					"fontsize": 9,
					"activebgcolor": [
						0.373,
						0.702,
						0.631,
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
						0.373,
						0.702,
						0.631,
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
							"parameter_longname": "Legato",
							"parameter_shortname": "Legato",
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
					"annotation_name": "Legato",
					"annotation": "Trim same-pitch overlaps and optionally close small gaps.",
					"id": "obj-23",
					"patching_rect": [
						324,
						84,
						44,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						324,
						84,
						44,
						20
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "scalesnap",
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
					"fontsize": 9,
					"activebgcolor": [
						0.373,
						0.702,
						0.631,
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
						0.373,
						0.702,
						0.631,
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
							"parameter_longname": "Scale Snap",
							"parameter_shortname": "Scale Snap",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_initial": [
								0
							],
							"parameter_initial_enable": 1,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Scale Snap",
					"annotation": "Move every pitch onto the chosen scale.",
					"id": "obj-24",
					"patching_rect": [
						372,
						84,
						44,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						372,
						84,
						44,
						20
					]
				}
			},
			{
				"box": {
					"maxclass": "live.text",
					"varname": "autokey",
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
					"fontsize": 9,
					"activebgcolor": [
						0.373,
						0.702,
						0.631,
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
						0.373,
						0.702,
						0.631,
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
							"parameter_longname": "Auto Key",
							"parameter_shortname": "Auto Key",
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
					"annotation_name": "Auto Key",
					"annotation": "Detect the key when a clip is loaded.",
					"id": "obj-25",
					"patching_rect": [
						420,
						84,
						44,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						420,
						84,
						44,
						20
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "velmin",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "VelMin",
							"parameter_shortname": "VelMin",
							"parameter_type": 1,
							"parameter_mmin": 1,
							"parameter_mmax": 127,
							"parameter_initial": [
								64
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 0,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "VelMin",
					"annotation": "Bottom of the target velocity range.",
					"id": "obj-26",
					"patching_rect": [
						146,
						110,
						50,
						62
					],
					"presentation": 1,
					"presentation_rect": [
						146,
						110,
						50,
						62
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "velmax",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "VelMax",
							"parameter_shortname": "VelMax",
							"parameter_type": 1,
							"parameter_mmin": 1,
							"parameter_mmax": 127,
							"parameter_initial": [
								112
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 0,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "VelMax",
					"annotation": "Top of the target velocity range.",
					"id": "obj-27",
					"patching_rect": [
						198,
						110,
						50,
						62
					],
					"presentation": 1,
					"presentation_rect": [
						198,
						110,
						50,
						62
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "velcomp",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Comp",
							"parameter_shortname": "Comp",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								40
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Comp",
					"annotation": "How hard velocities are compressed into Vel Min..Max.",
					"id": "obj-28",
					"patching_rect": [
						250,
						110,
						50,
						62
					],
					"presentation": 1,
					"presentation_rect": [
						250,
						110,
						50,
						62
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
					"fontsize": 10,
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
					"annotation": "Key centre for Scale Snap and variations.",
					"id": "obj-29",
					"patching_rect": [
						304,
						128,
						44,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						304,
						128,
						44,
						20
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
					"fontsize": 10,
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
								0
							],
							"parameter_initial_enable": 1,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Mode",
					"annotation": "Scale for Scale Snap and in-scale substitutions.",
					"id": "obj-30",
					"patching_rect": [
						352,
						128,
						112,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						352,
						128,
						112,
						20
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "VARIATIONS",
					"fontname": "Arial Bold",
					"fontsize": 10,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-31",
					"patching_rect": [
						516,
						6,
						90,
						12
					],
					"presentation": 1,
					"presentation_rect": [
						516,
						6,
						90,
						12
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "amount",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Amount",
							"parameter_shortname": "Amount",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								40
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Amount",
					"annotation": "Master variation amount. 0 leaves the clip unchanged.",
					"id": "obj-32",
					"patching_rect": [
						516,
						18,
						50,
						62
					],
					"presentation": 1,
					"presentation_rect": [
						516,
						18,
						50,
						62
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "rhythm",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Rhythm",
							"parameter_shortname": "Rhythm",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								50
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Rhythm",
					"annotation": "Shift, thin and fill.",
					"id": "obj-33",
					"patching_rect": [
						570,
						18,
						46,
						58
					],
					"presentation": 1,
					"presentation_rect": [
						570,
						18,
						46,
						58
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "melody",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Melody",
							"parameter_shortname": "Melody",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								50
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Melody",
					"annotation": "In-scale substitutions, octave jumps, inversion, retrograde.",
					"id": "obj-34",
					"patching_rect": [
						620,
						18,
						46,
						58
					],
					"presentation": 1,
					"presentation_rect": [
						620,
						18,
						46,
						58
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "ghosts",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Ghosts",
							"parameter_shortname": "Ghosts",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								30
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Ghosts",
					"annotation": "Quiet fills at the end of a phrase.",
					"id": "obj-35",
					"patching_rect": [
						516,
						82,
						46,
						58
					],
					"presentation": 1,
					"presentation_rect": [
						516,
						82,
						46,
						58
					]
				}
			},
			{
				"box": {
					"maxclass": "live.dial",
					"varname": "accents",
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
					"fontsize": 9,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Accent",
							"parameter_shortname": "Accent",
							"parameter_type": 1,
							"parameter_mmin": 0,
							"parameter_mmax": 100,
							"parameter_initial": [
								40
							],
							"parameter_initial_enable": 1,
							"parameter_unitstyle": 5,
							"parameter_invisible": 0
						}
					},
					"annotation_name": "Accent",
					"annotation": "Velocity accents on downbeats, quieter offbeats.",
					"id": "obj-36",
					"patching_rect": [
						566,
						82,
						46,
						58
					],
					"presentation": 1,
					"presentation_rect": [
						566,
						82,
						46,
						58
					]
				}
			},
			{
				"box": {
					"maxclass": "jsui",
					"filename": "dooblr_previews.js",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"parameter_enable": 0,
					"border": 0,
					"annotation_name": "Variations",
					"annotation": "Click a variation, then press Write.",
					"id": "obj-37",
					"patching_rect": [
						516,
						144,
						188,
						100
					],
					"presentation": 1,
					"presentation_rect": [
						516,
						144,
						188,
						100
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "SOURCE",
					"fontname": "Arial Bold",
					"fontsize": 10,
					"textcolor": [
						0.541,
						0.569,
						0.612,
						1
					],
					"ignoreclick": 1,
					"background": 0,
					"id": "obj-38",
					"patching_rect": [
						716,
						6,
						80,
						12
					],
					"presentation": 1,
					"presentation_rect": [
						716,
						6,
						80,
						12
					]
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
					"fontsize": 9,
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
					"annotation": "Drop a MIDI file (.mid) from Finder or Live's file browser. Session and Arrangement clips cannot be dropped in Live 11; use USE CLIP.",
					"id": "obj-39",
					"patching_rect": [
						716,
						20,
						136,
						36
					],
					"presentation": 1,
					"presentation_rect": [
						716,
						20,
						136,
						36
					]
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
					"fontsize": 10,
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
					"annotation": "Read the MIDI clip shown in the Detail View (or the selected clip).",
					"id": "obj-40",
					"patching_rect": [
						716,
						60,
						84,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						716,
						60,
						84,
						20
					]
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
					"fontsize": 8.5,
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
							"parameter_longname": "Clear Source",
							"parameter_shortname": "Clear Source",
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
					"annotation": "Forget the loaded clip.",
					"id": "obj-41",
					"patching_rect": [
						804,
						60,
						48,
						20
					],
					"presentation": 1,
					"presentation_rect": [
						804,
						60,
						48,
						20
					]
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
					"id": "obj-42",
					"patching_rect": [
						716,
						84,
						136,
						28
					],
					"presentation": 1,
					"presentation_rect": [
						716,
						84,
						136,
						28
					],
					"varname": "srcinfo"
				}
			},
			{
				"box": {
					"maxclass": "jsui",
					"filename": "dooblr_roll.js",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [
						""
					],
					"parameter_enable": 0,
					"border": 0,
					"annotation_name": "Clip",
					"annotation": "Notes of the loaded clip, and of the last Tighten or Write result.",
					"id": "obj-43",
					"patching_rect": [
						716,
						116,
						136,
						128
					],
					"presentation": 1,
					"presentation_rect": [
						716,
						116,
						136,
						128
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "live.thisdevice",
					"numinlets": 1,
					"numoutlets": 3,
					"id": "obj-44",
					"patching_rect": [
						900,
						20,
						90,
						22
					],
					"outlettype": [
						"bang",
						"int",
						"int"
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "init",
					"numinlets": 2,
					"numoutlets": 1,
					"id": "obj-45",
					"patching_rect": [
						900,
						52,
						34,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "js dooblr_titan.js",
					"numinlets": 1,
					"numoutlets": 4,
					"id": "obj-46",
					"patching_rect": [
						900,
						420,
						200,
						22
					],
					"outlettype": [
						"",
						"",
						"",
						""
					],
					"saved_object_attributes": {
						"filename": "dooblr_titan.js",
						"parameter_enable": 0
					}
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param qstrength",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-47",
					"patching_rect": [
						900,
						90,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param grid",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-48",
					"patching_rect": [
						1060,
						90,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param swing",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-49",
					"patching_rect": [
						1220,
						90,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param groove",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-50",
					"patching_rect": [
						1380,
						90,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param flams",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-51",
					"patching_rect": [
						1540,
						90,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param minlen",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-52",
					"patching_rect": [
						900,
						118,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param minvel",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-53",
					"patching_rect": [
						1060,
						118,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param legato",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-54",
					"patching_rect": [
						1220,
						118,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param gapclose",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-55",
					"patching_rect": [
						1380,
						118,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param velmin",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-56",
					"patching_rect": [
						1540,
						118,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param velmax",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-57",
					"patching_rect": [
						900,
						146,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param velcomp",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-58",
					"patching_rect": [
						1060,
						146,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param humanize",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-59",
					"patching_rect": [
						1220,
						146,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param scalesnap",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-60",
					"patching_rect": [
						1380,
						146,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param root",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-61",
					"patching_rect": [
						1540,
						146,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param mode",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-62",
					"patching_rect": [
						900,
						174,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param autokey",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-63",
					"patching_rect": [
						1060,
						174,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param amount",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-64",
					"patching_rect": [
						1220,
						174,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param rhythm",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-65",
					"patching_rect": [
						1380,
						174,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param melody",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-66",
					"patching_rect": [
						1540,
						174,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param ghosts",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-67",
					"patching_rect": [
						900,
						202,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param accents",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-68",
					"patching_rect": [
						1060,
						202,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend param seed",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-69",
					"patching_rect": [
						1220,
						202,
						150,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"id": "obj-70",
					"patching_rect": [
						900,
						260,
						80,
						22
					],
					"outlettype": [
						"",
						"",
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "tighten",
					"numinlets": 2,
					"numoutlets": 1,
					"id": "obj-71",
					"patching_rect": [
						900,
						292,
						80,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"id": "obj-72",
					"patching_rect": [
						1020,
						260,
						80,
						22
					],
					"outlettype": [
						"",
						"",
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "generate",
					"numinlets": 2,
					"numoutlets": 1,
					"id": "obj-73",
					"patching_rect": [
						1020,
						292,
						80,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"id": "obj-74",
					"patching_rect": [
						1140,
						260,
						80,
						22
					],
					"outlettype": [
						"",
						"",
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "another",
					"numinlets": 2,
					"numoutlets": 1,
					"id": "obj-75",
					"patching_rect": [
						1140,
						292,
						80,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"id": "obj-76",
					"patching_rect": [
						1260,
						260,
						80,
						22
					],
					"outlettype": [
						"",
						"",
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "write",
					"numinlets": 2,
					"numoutlets": 1,
					"id": "obj-77",
					"patching_rect": [
						1260,
						292,
						80,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"id": "obj-78",
					"patching_rect": [
						1380,
						260,
						80,
						22
					],
					"outlettype": [
						"",
						"",
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "useclip",
					"numinlets": 2,
					"numoutlets": 1,
					"id": "obj-79",
					"patching_rect": [
						1380,
						292,
						80,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"id": "obj-80",
					"patching_rect": [
						1500,
						260,
						80,
						22
					],
					"outlettype": [
						"",
						"",
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "clearsource",
					"numinlets": 2,
					"numoutlets": 1,
					"id": "obj-81",
					"patching_rect": [
						1500,
						292,
						80,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "route bang 1",
					"numinlets": 2,
					"numoutlets": 3,
					"id": "obj-82",
					"patching_rect": [
						1620,
						260,
						80,
						22
					],
					"outlettype": [
						"",
						"",
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "message",
					"text": "savemidi",
					"numinlets": 2,
					"numoutlets": 1,
					"id": "obj-83",
					"patching_rect": [
						1620,
						292,
						80,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend dropfile",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-84",
					"patching_rect": [
						900,
						330,
						110,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "midiin",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-85",
					"patching_rect": [
						1700,
						20,
						50,
						22
					],
					"outlettype": [
						"int"
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "midiout",
					"numinlets": 1,
					"numoutlets": 0,
					"id": "obj-86",
					"patching_rect": [
						1700,
						52,
						55,
						22
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "savedialog mid",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-87",
					"patching_rect": [
						1700,
						90,
						100,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "newobj",
					"text": "prepend savepath",
					"numinlets": 1,
					"numoutlets": 1,
					"id": "obj-88",
					"patching_rect": [
						1700,
						122,
						120,
						22
					],
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"text": "Controls send param <name> <value> to the js. Drop sends dropfile; buttons send tighten/generate/another/write/useclip/clearsource/savemidi. The js sends setstate to the roll and previews, set seed, and bang to savedialog.",
					"fontsize": 10,
					"id": "obj-89",
					"patching_rect": [
						900,
						460,
						520,
						48
					]
				}
			}
		],
		"lines": [
			{
				"patchline": {
					"source": [
						"obj-14",
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
						"obj-46",
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
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-15",
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
						"obj-46",
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
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-22",
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
						"obj-46",
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
						"obj-46",
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
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-23",
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
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-18",
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
						"obj-55",
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
						"obj-26",
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
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-27",
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
						"obj-57",
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
						"obj-28",
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
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-19",
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
						"obj-59",
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
						"obj-24",
						0
					],
					"destination": [
						"obj-60",
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
						"obj-46",
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
						"obj-61",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-61",
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
						"obj-30",
						0
					],
					"destination": [
						"obj-62",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-62",
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
						"obj-25",
						0
					],
					"destination": [
						"obj-63",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-63",
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
						"obj-32",
						0
					],
					"destination": [
						"obj-64",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-64",
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
						"obj-33",
						0
					],
					"destination": [
						"obj-65",
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
						"obj-46",
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
						"obj-66",
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
						"obj-46",
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
						"obj-67",
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
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-36",
						0
					],
					"destination": [
						"obj-68",
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
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-12",
						0
					],
					"destination": [
						"obj-69",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-69",
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
						"obj-6",
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
						"obj-71",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-70",
						1
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
						"obj-46",
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
						"obj-73",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						1
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
						"obj-46",
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
						"obj-75",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-74",
						1
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
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-9",
						0
					],
					"destination": [
						"obj-76",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-76",
						0
					],
					"destination": [
						"obj-77",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-76",
						1
					],
					"destination": [
						"obj-77",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-77",
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
						"obj-40",
						0
					],
					"destination": [
						"obj-78",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-78",
						0
					],
					"destination": [
						"obj-79",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-78",
						1
					],
					"destination": [
						"obj-79",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-79",
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
						"obj-41",
						0
					],
					"destination": [
						"obj-80",
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
						"obj-81",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-80",
						1
					],
					"destination": [
						"obj-81",
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
						"obj-46",
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
						"obj-82",
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
						"obj-83",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-82",
						1
					],
					"destination": [
						"obj-83",
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
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-39",
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
						"obj-46",
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
						"obj-86",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-46",
						3
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
						"obj-88",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-88",
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
						"obj-44",
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
						"obj-43",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-46",
						1
					],
					"destination": [
						"obj-12",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-46",
						2
					],
					"destination": [
						"obj-37",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-37",
						0
					],
					"destination": [
						"obj-46",
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
