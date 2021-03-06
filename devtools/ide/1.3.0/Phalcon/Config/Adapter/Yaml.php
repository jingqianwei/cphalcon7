<?php 

namespace Phalcon\Config\Adapter {

	/**
	 * Phalcon\Config\Adapter\Yaml
	 *
	 * Reads YAML files and converts them to Phalcon\Config objects.
	 *
	 * Given the following configuration file:
	 *
	 *<code>
	 *phalcon
	 *  baseuri: /phalcon/
	 *models:
	 *  metadata: memory
	 *</code>
	 *
	 * You can read it as follows:
	 *
	 *<code>
	 *	$config = new Phalcon\Config\Adapter\Yaml("path/config.yaml");
	 *	echo $config->phalcon->baseuri;
	 *	echo $config->models->metadata;
	 *</code>
	 *
	 */
	
	class Yaml extends \Phalcon\Config\Adapter implements \Phalcon\Config\AdapterInterface, \ArrayAccess, \Countable {

		/**
		 * Load config file
		 *
		 * @param string $filePath
		 */
		public function read($filePath, $absolutePath=null){ }

	}
}
