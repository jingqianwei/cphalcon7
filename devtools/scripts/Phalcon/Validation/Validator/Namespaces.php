<?php

/*
 +------------------------------------------------------------------------+
 | Phalcon Framework                                                      |
 +------------------------------------------------------------------------+
 | Copyright (c) 2011-2016 Phalcon Team (http://www.phalconphp.com)       |
 +------------------------------------------------------------------------+
 | This source file is subject to the New BSD License that is bundled     |
 | with this package in the file docs/LICENSE.txt.                        |
 |                                                                        |
 | If you did not receive a copy of the license and are unable to         |
 | obtain it through the world-wide-web, please send an email             |
 | to license@phalconphp.com so we can send you a copy immediately.       |
 +------------------------------------------------------------------------+
 | Authors: Serghei Iakovlev <serghei@phalconphp.com>                     |
 +------------------------------------------------------------------------+
 */

namespace Phalcon\Validation\Validator;

use Phalcon\Validation;
use Phalcon\Validation\Validator;
use Phalcon\Validation\Message;
use Phalcon\Validation\ValidatorInterface;

/**
 * Phalcon\Validation\Validator\Namespaces
 *
 * Check for namespace
 *
 *<code>
 *use Phalcon\Validation\Validator\Namespaces as NSValidator;
 *
 *$validation->add('namespace', new Namespaces(array(
 *    'allowEmpty' => true,
 *    'message' => ':field must be a valid namespace'
 *)));
 *</code>
 *
 * @package Phalcon\Validation\Validator
 */
class Namespaces extends Validator implements ValidatorInterface
{
    /**
     * Executes the namespaces validation
     *
     * @param Validation $validation
     * @param string $field
     *
     * @return bool
     */
    public function validate(\Phalcon\ValidationInterface $validation, $field, bool $allowEmpty = NULL)
    {
        $value = $validation->getValue($field);

        if ($allowEmpty && empty($value)) {
            return true;
        }

        if (!$this->valid($value)) {
            $label = $this->getOption('label') ?: $validation->getLabel($field);
            $message = $this->getOption('message') ?: 'Invalid namespace syntax!';
            $replacePairs = array(':field' => $label);

            $validation->appendMessage(new Message(strtr($message, $replacePairs), $field, 'Namespaces'));
            return false;
        }

        return true;
    }

    /**
     * Executes the validation
     *
     * @param mixed $value
     *
     * @return bool
     */
    public function valid($value)
    {
        $re = '#^(?:(?:\\\)?[a-z](?:[a-z0-9_]+)?)+(?:\\\\(?:[a-z](?:[a-z0-9_]+)?)+)*$#i';

        return (false !== (bool) preg_match($re, $value));
    }
}
