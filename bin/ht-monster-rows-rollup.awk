BEGIN { key=""; total=0; }
{ if (key == "") {
	key = $1;
    }
    else {
	if (key == $1) {
      total+=$2
	}
	else {
	    print total, "\t", key;
	    key = $1;
	    total = $2;
	}
    }
}
END { print total, "\t", key; }
