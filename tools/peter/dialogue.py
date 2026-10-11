"""The phone call: one timing table shared by the Blender clip, the audio track and the subtitles.

Joe calls; Peter answers. Original lines written for OOT Royale (no show dialogue or recordings).
Times are seconds from the first ring. Speaker 0 = Joe (on the phone), 1 = Peter.
"""
RING_END=1.7        # two rings, then he digs the phone out
RAISE=(1.5,2.15)    # hand from hip to ear
LOWER=(25.6,26.3)   # hangs up, phone back in pocket
DURATION=26.6
LINES=[
    (0,2.3,3.3,"Hey Peter!"),
    (1,3.5,5.0,"Hey Joe! What's goin' on?"),
    (0,5.2,7.6,"There's a giant storm wall closing in on Hyrule Field. You safe?"),
    (1,7.8,10.4,"Relax, Joe. Some kid in a green dress paid me rupees to punch stuff."),
    (0,10.6,12.6,"Rupees? Peter, that's not real money."),
    (1,12.8,15.6,"Joe, I just opened a treasure chest and there was a fish in it. Nothing's real anymore."),
    (0,15.8,17.4,"How'd you even end up in there?"),
    # Dedication: Cynthia asked for Peter as a joke, and he became a real game asset.
    (1,17.6,21.0,"Cynthia, Joe. She said 'put Peter in the game' as a joke. Now I'm a real game asset!"),
    (0,21.2,23.0,"...Well, tell Cynthia I said hi."),
    (1,23.2,25.4,"Heh heh heh heh. Bye, Joe."),
]
# Short words spoken with the formant voice; the rest of each line is Zelda-style text-box babble.
SPOKEN={0:"hey peter",1:"hey joe",7:"cynthia",9:"bye joe"}
# Quip bubbles over his head (index order is the runtime's kQuip* enum in RoyalePeter.h).
QUIPS=["Haymaker!","Belly bump!","Chicken fight!","Ssss... ahh!","Heh heh heh.","Freakin' sweet!",
       "Cynthia put me in this game!","Roadhouse!"]
