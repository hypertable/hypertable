#!/usr/bin/env python

import random
import sys
import time
from hypertable.thrift_client.thriftclient import ThriftClient
from hypertable.thrift_client.hyperthrift.gen.ttypes import *

story = \
"""In 1815, M. Charles-Francois-Bienvenu Myriel was Bishop of D---- He was
an old man of about seventy-five years of age; he had occupied the see
of D---- since 1806.

Although this detail has no connection whatever with the real substance
of what we are about to relate, it will not be superfluous, if merely
for the sake of exactness in all points, to mention here the various
rumors and remarks which had been in circulation about him from the very
moment when he arrived in the diocese. True or false, that which is said
of men often occupies as important a place in their lives, and above all
in their destinies, as that which they do. M. Myriel was the son of a
councillor of the Parliament of Aix; hence he belonged to the nobility
of the bar. It was said that his father, destining him to be the heir of
his own post, had married him at a very early age, eighteen or twenty,
in accordance with a custom which is rather widely prevalent in
parliamentary families. In spite of this marriage, however, it was said
that Charles Myriel created a great deal of talk. He was well formed,
though rather short in stature, elegant, graceful, intelligent; the
whole of the first portion of his life had been devoted to the world and
to gallantry.

The Revolution came; events succeeded each other with precipitation; the
parliamentary families, decimated, pursued, hunted down, were dispersed.
M. Charles Myriel emigrated to Italy at the very beginning of the
Revolution. There his wife died of a malady of the chest, from which she
had long suffered. He had no children. What took place next in the fate
of M. Myriel? The ruin of the French society of the olden days, the fall
of his own family, the tragic spectacles of '93, which were, perhaps,
even more alarming to the emigrants who viewed them from a distance,
with the magnifying powers of terror,--did these cause the ideas of
renunciation and solitude to germinate in him? Was he, in the midst of
these distractions, these affections which absorbed his life, suddenly
smitten with one of those mysterious and terrible blows which sometimes
overwhelm, by striking to his heart, a man whom public catastrophes
would not shake, by striking at his existence and his fortune? No one
could have told: all that was known was, that when he returned from
Italy he was a priest.

In 1804, M. Myriel was the Cure of B---- [Brignolles]. He was already
advanced in years, and lived in a very retired manner.

About the epoch of the coronation, some petty affair connected with
his curacy--just what, is not precisely known--took him to Paris.
Among other powerful persons to whom he went to solicit aid for his
parishioners was M. le Cardinal Fesch. One day, when the Emperor
had come to visit his uncle, the worthy Cure, who was waiting in the
anteroom, found himself present when His Majesty passed. Napoleon,
on finding himself observed with a certain curiosity by this old man,
turned round and said abruptly:--

Who is this good man who is staring at me?

Sire, said M. Myriel, you are looking at a good man, and I at a great
man. Each of us can profit by it.

That very evening, the Emperor asked the Cardinal the name of the Cure,
and some time afterwards M. Myriel was utterly astonished to learn that
he had been appointed Bishop of D----

What truth was there, after all, in the stories which were invented as
to the early portion of M. Myriel's life? No one knew. Very few families
had been acquainted with the Myriel family before the Revolution.

M. Myriel had to undergo the fate of every newcomer in a little town,
where there are many mouths which talk, and very few heads which think.
He was obliged to undergo it although he was a bishop, and because
he was a bishop. But after all, the rumors with which his name
was connected were rumors only,--noise, sayings, words; less than
words--palabres, as the energetic language of the South expresses it.

However that may be, after nine years of episcopal power and of
residence in D----, all the stories and subjects of conversation which
engross petty towns and petty people at the outset had fallen into
profound oblivion. No one would have dared to mention them; no one would
have dared to recall them.

M. Myriel had arrived at D---- accompanied by an elderly spinster,
Mademoiselle Baptistine, who was his sister, and ten years his junior.
""";

if len(sys.argv) < 2:
    print (sys.argv[0], "<amount-to-write>")
    sys.exit(1)

try:
    random.seed(1)
    story = story.replace("\n", " ")

    client = ThriftClient("localhost", 15867)

    namespace = client.namespace_open("/")

    mutator = client.mutator_open(namespace, "LargeRowTest", 0, 0)

    iterations = int(sys.argv[1]) / len(story)

    for x in range(0, iterations):
        cutoff = random.randint(0, len(story))
        story_cut = story[cutoff:] + story[:cutoff]
        client.mutator_set_cell(mutator, Cell(Key(story_cut, "c", None), "dummy"))

    client.mutator_flush(mutator)

    client.close_namespace(namespace)

except ClientException, e:
    print ('%s' % e.message)
    sys.exit(1)

sys.exit(0)
