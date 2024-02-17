<?php

$file = "gng-level1.tmx";

if (file_exists($file)) {
    $xml = simplexml_load_file($file);
    $json = json_encode($xml);
    $array = json_decode($json, TRUE);

    foreach ($array["layer"] as $key => $value) {
        $data = $value["data"];
        echo "u16 tmx_" . $value["@attributes"]["name"] . "[15][224] = {\n";
        $ligne_cpt = 0;
        foreach (explode("\n", $data) as $ligne) {
            if (!empty($ligne)) {
                $ligne_cpt++;
                $ligne = rtrim($ligne, ",");

                $display = true;
                if ($value["@attributes"]["name"] == "herbe" && $ligne_cpt < 14) {
                    //$display = false;
                }

                if ($display) {
                    echo "\t{" . $ligne . "},\n";
                }
            }
        }
        echo "};\n";
    }
}
